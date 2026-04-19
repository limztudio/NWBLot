// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/scene/scene.h>
#include <core/ecs/ecs.h>
#include <core/common/common.h>

#include <global/compile.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TestContext{
    u32 passed = 0;
    u32 failed = 0;

    void checkTrue(const bool condition, const char* expression, const char* file, const int line){
        if(condition){
            ++passed;
            return;
        }

        ++failed;
        NWB_CERR << file << '(' << line << "): check failed: " << expression << '\n';
    }
};


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
    NWB_SCENE_TEST_CHECK(context, (reinterpret_cast<usize>(&camera.projection) % alignof(AlignedFloat4Data)) == 0);
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
    pointTransform.position = AlignedFloat3Data(1.0f, 2.0f, 3.0f);
    pointLight.type = NWB::Core::Scene::LightType::Point;
    pointLight.setColor(AlignedFloat3Data(1.0f, 0.75f, 0.5f));
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


int main(){
    NWB::Core::Common::InitializerGuard commonInitializerGuard;
    if(!commonInitializerGuard.initialize()){
        NWB_CERR << "scene tests failed: common initialization failed\n";
        return 1;
    }

    __hidden_scene_tests::TestContext context;
    __hidden_scene_tests::TestSceneAndMainCamera(context);
    __hidden_scene_tests::TestLightComponents(context);

    if(context.failed != 0){
        NWB_CERR << "scene tests failed: " << context.failed << " of " << (context.passed + context.failed) << '\n';
        return 1;
    }

    NWB_COUT << "scene tests passed: " << context.passed << '\n';
    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

