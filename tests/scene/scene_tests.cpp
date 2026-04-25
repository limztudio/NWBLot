// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/scene/scene.h>
#include <core/ecs/ecs.h>
#include <core/common/common.h>

#include <tests/test_context.h>

#include <global/compile.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;


#define NWB_SCENE_TEST_CHECK(context, expression) (context).checkTrue((expression), #expression, __FILE__, __LINE__)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void* SceneTestAlloc(usize size){
    return NWB::Core::Alloc::CoreAlloc(size, "NWB::Tests::Scene::Alloc");
}

static void SceneTestFree(void* ptr){
    NWB::Core::Alloc::CoreFree(ptr, "NWB::Tests::Scene::Free");
}

static void* SceneTestAllocAligned(usize size, usize align){
    return NWB::Core::Alloc::CoreAllocAligned(size, align, "NWB::Tests::Scene::AllocAligned");
}

static void SceneTestFreeAligned(void* ptr){
    NWB::Core::Alloc::CoreFreeAligned(ptr, "NWB::Tests::Scene::FreeAligned");
}


struct TestWorld{
    NWB::Core::Alloc::CustomArena arena;
    NWB::Core::Alloc::ThreadPool threadPool;
    NWB::Core::ECS::World world;

    TestWorld()
        : arena(&SceneTestAlloc, &SceneTestFree, &SceneTestAllocAligned, &SceneTestFreeAligned)
        , threadPool(0)
        , world(arena, threadPool)
    {}
};


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

static void TestCameraProjectionHelpers(TestContext& context){
    NWB::Core::Scene::CameraComponent camera;

    f32 tanHalfFov = 0.0f;
    NWB_SCENE_TEST_CHECK(
        context,
        NWB::Core::Scene::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)
    );
    NWB_SCENE_TEST_CHECK(context, tanHalfFov > 0.0f);
    NWB_SCENE_TEST_CHECK(context, NWB::Core::Scene::CameraClipRangeValid(camera));
    NWB_SCENE_TEST_CHECK(context, NWB::Core::Scene::ResolveCameraAspectRatio(camera, 1.5f) == 1.5f);

    camera.setAspectRatio(2.0f);
    NWB_SCENE_TEST_CHECK(context, NWB::Core::Scene::ResolveCameraAspectRatio(camera, 1.5f) == 2.0f);

    Float4 projectionParams;
    NWB_SCENE_TEST_CHECK(context, NWB::Core::Scene::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams));
    NWB_SCENE_TEST_CHECK(context, projectionParams.x > 0.0f);
    NWB_SCENE_TEST_CHECK(context, projectionParams.y > 0.0f);
    NWB_SCENE_TEST_CHECK(context, projectionParams.z > 0.0f);
    NWB_SCENE_TEST_CHECK(context, projectionParams.w < 0.0f);

    NWB::Core::Scene::CameraProjectionData projectionData;
    NWB_SCENE_TEST_CHECK(context, NWB::Core::Scene::TryBuildCameraProjectionData(camera, 1.5f, projectionData));
    NWB_SCENE_TEST_CHECK(context, NWB::Core::Scene::CameraProjectionDataValid(projectionData));
    NWB_SCENE_TEST_CHECK(context, projectionData.projectionParams.x == projectionParams.x);
    NWB_SCENE_TEST_CHECK(context, projectionData.projectionParams.y == projectionParams.y);
    NWB_SCENE_TEST_CHECK(context, projectionData.projectionParams.z == projectionParams.z);
    NWB_SCENE_TEST_CHECK(context, projectionData.projectionParams.w == projectionParams.w);
    NWB_SCENE_TEST_CHECK(context, projectionData.aspectRatio == 2.0f);
    NWB_SCENE_TEST_CHECK(context, projectionData.tanHalfVerticalFov > 0.0f);

    camera.setNearPlane(0.0f);
    NWB_SCENE_TEST_CHECK(context, !NWB::Core::Scene::CameraClipRangeValid(camera));
    NWB_SCENE_TEST_CHECK(context, !NWB::Core::Scene::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams));
    NWB_SCENE_TEST_CHECK(
        context,
        !NWB::Core::Scene::CameraProjectionDataValid(NWB::Core::Scene::CameraProjectionData{})
    );

    camera = NWB::Core::Scene::CameraComponent{};
    camera.setNearPlane(2.0f);
    camera.setFarPlane(s_MaxF32);
    NWB_SCENE_TEST_CHECK(context, NWB::Core::Scene::CameraClipRangeValid(camera));
    NWB_SCENE_TEST_CHECK(context, !NWB::Core::Scene::TryBuildCameraProjectionParams(camera, 1.5f, projectionParams));

    camera = NWB::Core::Scene::CameraComponent{};
    camera.setVerticalFovRadians(179.0f * (s_PI / 180.0f));
    NWB_SCENE_TEST_CHECK(context, !NWB::Core::Scene::TryBuildCameraProjectionParams(camera, s_MaxF32, projectionParams));

    camera.setAspectRatio(s_MaxF32);
    NWB_SCENE_TEST_CHECK(context, !NWB::Core::Scene::TryBuildCameraProjectionData(camera, 1.5f, projectionData));

    camera = NWB::Core::Scene::CameraComponent{};
    camera.setVerticalFovRadians(180.0f * (s_PI / 180.0f));
    NWB_SCENE_TEST_CHECK(
        context,
        !NWB::Core::Scene::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)
    );
    camera.setVerticalFovRadians(400.0f * (s_PI / 180.0f));
    NWB_SCENE_TEST_CHECK(
        context,
        !NWB::Core::Scene::TryComputeCameraTanHalfVerticalFov(camera.verticalFovRadians(), tanHalfFov)
    );
}

static void TestLightComponents(TestContext& context){
    TestWorld testWorld;

    auto directionalEntity = testWorld.world.createEntity();
    auto& directionalTransform = directionalEntity.addComponent<NWB::Core::Scene::TransformComponent>();
    auto& directionalLight = directionalEntity.addComponent<NWB::Core::Scene::LightComponent>();

    NWB_SCENE_TEST_CHECK(context, directionalEntity.hasComponent<NWB::Core::Scene::TransformComponent>());
    NWB_SCENE_TEST_CHECK(context, directionalEntity.hasComponent<NWB::Core::Scene::LightComponent>());
    NWB_SCENE_TEST_CHECK(context, directionalLight.type == NWB::Core::Scene::LightType::Directional);
    NWB_SCENE_TEST_CHECK(context, directionalLight.color().x == 1.0f);
    NWB_SCENE_TEST_CHECK(context, directionalLight.color().y == 1.0f);
    NWB_SCENE_TEST_CHECK(context, directionalLight.color().z == 1.0f);
    NWB_SCENE_TEST_CHECK(context, directionalLight.intensity() > 0.0f);
    NWB_SCENE_TEST_CHECK(context, directionalLight.range > 0.0f);
    NWB_SCENE_TEST_CHECK(context, directionalTransform.rotation.w == 1.0f);

    auto pointEntity = testWorld.world.createEntity();
    auto& pointTransform = pointEntity.addComponent<NWB::Core::Scene::TransformComponent>();
    auto& pointLight = pointEntity.addComponent<NWB::Core::Scene::LightComponent>();
    pointTransform.position = Float4(1.0f, 2.0f, 3.0f);
    pointLight.type = NWB::Core::Scene::LightType::Point;
    pointLight.setColor(Float4(1.0f, 0.75f, 0.5f));
    pointLight.setIntensity(4.0f);
    pointLight.range = 12.0f;

    NWB_SCENE_TEST_CHECK(context, pointEntity.hasComponent<NWB::Core::Scene::TransformComponent>());
    NWB_SCENE_TEST_CHECK(context, pointEntity.hasComponent<NWB::Core::Scene::LightComponent>());
    NWB_SCENE_TEST_CHECK(context, pointLight.type == NWB::Core::Scene::LightType::Point);
    NWB_SCENE_TEST_CHECK(context, pointLight.color().x == 1.0f);
    NWB_SCENE_TEST_CHECK(context, pointLight.color().y == 0.75f);
    NWB_SCENE_TEST_CHECK(context, pointLight.color().z == 0.5f);
    NWB_SCENE_TEST_CHECK(context, pointLight.intensity() == 4.0f);
    NWB_SCENE_TEST_CHECK(context, pointLight.range == 12.0f);

    NWB_SCENE_TEST_CHECK(
        context,
        (reinterpret_cast<usize>(&directionalLight) % alignof(NWB::Core::Scene::LightComponent)) == 0
    );
    NWB_SCENE_TEST_CHECK(context, (reinterpret_cast<usize>(&pointLight) % alignof(NWB::Core::Scene::LightComponent)) == 0);

    const NWB::Core::ECS::EntityID pointEntityId = pointEntity.id();
    usize lightViewCount = 0;
    usize directionalLightCount = 0;
    usize pointLightCount = 0;
    testWorld.world.view<
        NWB::Core::Scene::TransformComponent,
        NWB::Core::Scene::LightComponent
    >().each(
        [&context, &lightViewCount, &directionalLightCount, &pointLightCount, pointEntityId](
            NWB::Core::ECS::EntityID entityId,
            NWB::Core::Scene::TransformComponent& viewTransform,
            NWB::Core::Scene::LightComponent& viewLight
        ){
            ++lightViewCount;
            if(viewLight.type == NWB::Core::Scene::LightType::Directional){
                ++directionalLightCount;
                NWB_SCENE_TEST_CHECK(context, viewLight.intensity() > 0.0f);
            }
            else if(viewLight.type == NWB::Core::Scene::LightType::Point){
                ++pointLightCount;
                NWB_SCENE_TEST_CHECK(context, entityId == pointEntityId);
                NWB_SCENE_TEST_CHECK(context, viewTransform.position.x == 1.0f);
                NWB_SCENE_TEST_CHECK(context, viewLight.range > 0.0f);
            }
            else{
                NWB_SCENE_TEST_CHECK(context, false);
            }
        }
    );
    NWB_SCENE_TEST_CHECK(context, lightViewCount == 2);
    NWB_SCENE_TEST_CHECK(context, directionalLightCount == 1);
    NWB_SCENE_TEST_CHECK(context, pointLightCount == 1);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_SCENE_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int EntryPoint(const isize argc, tchar** argv, void*){
    static_cast<void>(argc);
    static_cast<void>(argv);

    NWB::Core::Common::InitializerGuard commonInitializerGuard;
    if(!commonInitializerGuard.initialize()){
        NWB_CERR << "scene tests failed: common initialization failed\n";
        return -1;
    }

    __hidden_scene_tests::TestContext context;
    __hidden_scene_tests::TestSceneAndMainCamera(context);
    __hidden_scene_tests::TestSceneCameraResolution(context);
    __hidden_scene_tests::TestCameraProjectionHelpers(context);
    __hidden_scene_tests::TestLightComponents(context);

    if(context.failed != 0){
        NWB_CERR << "scene tests failed: " << context.failed << " of " << (context.passed + context.failed) << '\n';
        return -1;
    }

    NWB_COUT << "scene tests passed: " << context.passed << '\n';
    return 0;
}


#include <global/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

