// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/scene/scene.h>
#include <core/scene/camera_component.h>
#include <core/scene/transform_component.h>
#include <core/ecs/ecs.h>
#include <core/common/common.h>

#include <tests/ecs_test_world.h>
#include <tests/test_context.h>

#include <global/compile.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;


#define NWB_SCENE_TEST_CHECK NWB_TEST_CHECK


struct SceneTestAllocatorTag;
using SceneTestAllocator = NWB::Tests::CountingTestAllocator<SceneTestAllocatorTag>;
using TestWorld = NWB::Tests::EcsTestWorldWithAllocator<SceneTestAllocator>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestSceneAndMainCamera(TestContext& context){
    TestWorld testWorld;

    auto sceneEntity = testWorld.world.createEntity();
    auto& scene = sceneEntity.addComponent<NWB::Core::Scene::SceneComponent>();
    NWB_SCENE_TEST_CHECK(context, scene.mainCamera == NWB::Core::ECS::ENTITY_ID_INVALID);

    auto cameraEntity = testWorld.world.createEntity();
    auto& transform = cameraEntity.addComponent<NWB::Core::Scene::TransformComponent>();
    auto& camera = cameraEntity.addComponent<NWB::Core::Scene::CameraComponent>();
    scene.mainCamera = cameraEntity.id();

    NWB_SCENE_TEST_CHECK(context, sceneEntity.hasComponent<NWB::Core::Scene::SceneComponent>());
    NWB_SCENE_TEST_CHECK(context, cameraEntity.hasComponent<NWB::Core::Scene::TransformComponent>());
    NWB_SCENE_TEST_CHECK(context, cameraEntity.hasComponent<NWB::Core::Scene::CameraComponent>());
    NWB_SCENE_TEST_CHECK(context, scene.mainCamera == cameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, scene.mainCamera.valid());
    NWB_SCENE_TEST_CHECK(context, testWorld.world.entityCount() == 2);

    NWB_SCENE_TEST_CHECK(
        context,
        (reinterpret_cast<usize>(&transform) % alignof(NWB::Core::Scene::TransformComponent)) == 0
    );
    NWB_SCENE_TEST_CHECK(context, (reinterpret_cast<usize>(&camera) % alignof(NWB::Core::Scene::CameraComponent)) == 0);
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
        NWB::Core::Scene::TransformComponent,
        NWB::Core::Scene::CameraComponent
    >().each(
        [&context, &cameraViewCount, cameraEntityId](
            NWB::Core::ECS::EntityID entityId,
            NWB::Core::Scene::TransformComponent& viewTransform,
            NWB::Core::Scene::CameraComponent& viewCamera
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

    NWB::Core::Scene::SceneCameraView emptyCameraView = NWB::Core::Scene::ResolveSceneCameraView(testWorld.world);
    NWB_SCENE_TEST_CHECK(context, !emptyCameraView.valid());

    auto firstCameraEntity = testWorld.world.createEntity();
    firstCameraEntity.addComponent<NWB::Core::Scene::TransformComponent>();
    firstCameraEntity.addComponent<NWB::Core::Scene::CameraComponent>();

    auto secondCameraEntity = testWorld.world.createEntity();
    secondCameraEntity.addComponent<NWB::Core::Scene::TransformComponent>();
    secondCameraEntity.addComponent<NWB::Core::Scene::CameraComponent>();

    auto* firstTransform = testWorld.world.tryGetComponent<NWB::Core::Scene::TransformComponent>(firstCameraEntity.id());
    auto* firstCamera = testWorld.world.tryGetComponent<NWB::Core::Scene::CameraComponent>(firstCameraEntity.id());
    auto* secondTransform = testWorld.world.tryGetComponent<NWB::Core::Scene::TransformComponent>(secondCameraEntity.id());
    auto* secondCamera = testWorld.world.tryGetComponent<NWB::Core::Scene::CameraComponent>(secondCameraEntity.id());

    NWB::Core::Scene::SceneCameraView fallbackCameraView = NWB::Core::Scene::ResolveSceneCameraView(testWorld.world);
    NWB_SCENE_TEST_CHECK(context, fallbackCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, fallbackCameraView.entity == firstCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, fallbackCameraView.transform == firstTransform);
    NWB_SCENE_TEST_CHECK(context, fallbackCameraView.camera == firstCamera);
    NWB_SCENE_TEST_CHECK(context, fallbackCameraView.projectionData.projectionParams.x > 0.0f);
    NWB_SCENE_TEST_CHECK(context, fallbackCameraView.projectionData.tanHalfVerticalFov > 0.0f);

    auto sceneEntity = testWorld.world.createEntity();
    auto& scene = sceneEntity.addComponent<NWB::Core::Scene::SceneComponent>();
    scene.mainCamera = secondCameraEntity.id();

    NWB::Core::Scene::SceneCameraView requestedCameraView = NWB::Core::Scene::ResolveSceneCameraView(testWorld.world);
    NWB_SCENE_TEST_CHECK(context, requestedCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, requestedCameraView.entity == secondCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, requestedCameraView.transform == secondTransform);
    NWB_SCENE_TEST_CHECK(context, requestedCameraView.camera == secondCamera);
    NWB_SCENE_TEST_CHECK(context, requestedCameraView.projectionData.projectionParams.y > 0.0f);
    NWB_SCENE_TEST_CHECK(context, requestedCameraView.projectionData.aspectRatio == 1.0f);

    NWB::Core::Scene::SceneCameraView invalidProjectionView = requestedCameraView;
    invalidProjectionView.projectionData = NWB::Core::Scene::CameraProjectionData{};
    NWB_SCENE_TEST_CHECK(context, !invalidProjectionView.valid());

    secondCamera->setNearPlane(0.0f);
    NWB::Core::Scene::SceneCameraView invalidRequestedFallbackCameraView =
        NWB::Core::Scene::ResolveSceneCameraView(testWorld.world)
    ;
    NWB_SCENE_TEST_CHECK(context, invalidRequestedFallbackCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, invalidRequestedFallbackCameraView.entity == firstCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, invalidRequestedFallbackCameraView.transform == firstTransform);
    NWB_SCENE_TEST_CHECK(context, invalidRequestedFallbackCameraView.camera == firstCamera);
    secondCamera->setNearPlane(NWB::Core::Scene::CameraComponent{}.nearPlane());

    secondTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    NWB::Core::Scene::SceneCameraView invalidTransformFallbackCameraView =
        NWB::Core::Scene::ResolveSceneCameraView(testWorld.world)
    ;
    NWB_SCENE_TEST_CHECK(context, invalidTransformFallbackCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, invalidTransformFallbackCameraView.entity == firstCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, invalidTransformFallbackCameraView.transform == firstTransform);
    NWB_SCENE_TEST_CHECK(context, invalidTransformFallbackCameraView.camera == firstCamera);
    secondTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    secondCamera->setVerticalFovRadians(179.0f * (s_PI / 180.0f));
    secondCamera->setAspectRatio(s_MaxF32);
    NWB::Core::Scene::SceneCameraView invalidProjectionFallbackCameraView =
        NWB::Core::Scene::ResolveSceneCameraView(testWorld.world)
    ;
    NWB_SCENE_TEST_CHECK(context, invalidProjectionFallbackCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, invalidProjectionFallbackCameraView.entity == firstCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, invalidProjectionFallbackCameraView.transform == firstTransform);
    NWB_SCENE_TEST_CHECK(context, invalidProjectionFallbackCameraView.camera == firstCamera);
    *secondCamera = NWB::Core::Scene::CameraComponent{};

    secondCamera->setVerticalFovRadians(179.0f * (s_PI / 180.0f));
    NWB::Core::Scene::SceneCameraView invalidFallbackAspectCameraView =
        NWB::Core::Scene::ResolveSceneCameraView(testWorld.world, s_MaxF32)
    ;
    NWB_SCENE_TEST_CHECK(context, invalidFallbackAspectCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, invalidFallbackAspectCameraView.entity == firstCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, invalidFallbackAspectCameraView.transform == firstTransform);
    NWB_SCENE_TEST_CHECK(context, invalidFallbackAspectCameraView.camera == firstCamera);
    *secondCamera = NWB::Core::Scene::CameraComponent{};

    secondTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 2.0f);
    NWB::Core::Scene::SceneCameraView nonUnitTransformFallbackCameraView =
        NWB::Core::Scene::ResolveSceneCameraView(testWorld.world)
    ;
    NWB_SCENE_TEST_CHECK(context, nonUnitTransformFallbackCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, nonUnitTransformFallbackCameraView.entity == firstCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, nonUnitTransformFallbackCameraView.transform == firstTransform);
    NWB_SCENE_TEST_CHECK(context, nonUnitTransformFallbackCameraView.camera == firstCamera);
    secondTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    f32 nonFiniteScale = s_MaxF32;
    nonFiniteScale *= 2.0f;
    secondTransform->scale = Float4(nonFiniteScale, 1.0f, 1.0f);
    NWB::Core::Scene::SceneCameraView nonFiniteScaleFallbackCameraView =
        NWB::Core::Scene::ResolveSceneCameraView(testWorld.world)
    ;
    NWB_SCENE_TEST_CHECK(context, nonFiniteScaleFallbackCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, nonFiniteScaleFallbackCameraView.entity == firstCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, nonFiniteScaleFallbackCameraView.transform == firstTransform);
    NWB_SCENE_TEST_CHECK(context, nonFiniteScaleFallbackCameraView.camera == firstCamera);
    secondTransform->scale = Float4(1.0f, 1.0f, 1.0f);

    auto staleMainCameraEntity = testWorld.world.createEntity();
    scene.mainCamera = staleMainCameraEntity.id();

    NWB::Core::Scene::SceneCameraView staleFallbackCameraView = NWB::Core::Scene::ResolveSceneCameraView(testWorld.world);
    NWB_SCENE_TEST_CHECK(context, staleFallbackCameraView.valid());
    NWB_SCENE_TEST_CHECK(context, staleFallbackCameraView.entity == firstCameraEntity.id());
    NWB_SCENE_TEST_CHECK(context, staleFallbackCameraView.transform == firstTransform);
    NWB_SCENE_TEST_CHECK(context, staleFallbackCameraView.camera == firstCamera);

    firstTransform->rotation = Float4(0.0f, 0.0f, 0.0f, 2.0f);
    NWB::Core::Scene::SceneCameraView invalidFallbackSkippedCameraView =
        NWB::Core::Scene::ResolveSceneCameraView(testWorld.world)
    ;
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


static int EntryPoint(const isize argc, tchar** argv, void*){
    static_cast<void>(argc);
    static_cast<void>(argv);

    return NWB::Tests::RunTestSuite("scene", [](NWB::Tests::TestContext& context){
        __hidden_scene_tests::TestSceneAndMainCamera(context);
        __hidden_scene_tests::TestSceneCameraResolution(context);
    });
}


#include <core/common/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

