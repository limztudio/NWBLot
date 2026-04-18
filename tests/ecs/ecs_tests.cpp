// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/ecs/ecs.h>
#include <core/common/common.h>

#include <global/compile.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_tests{


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


#define NWB_ECS_TEST_CHECK(context, expression) (context).checkTrue((expression), #expression, __FILE__, __LINE__)


static void* ECSTestAlloc(usize size){
    return NWB::Core::Alloc::CoreAlloc(size, "NWB::Tests::ECS::Alloc");
}

static void ECSTestFree(void* ptr){
    NWB::Core::Alloc::CoreFree(ptr, "NWB::Tests::ECS::Free");
}

static void* ECSTestAllocAligned(usize size, usize align){
    return NWB::Core::Alloc::CoreAllocAligned(size, align, "NWB::Tests::ECS::AllocAligned");
}

static void ECSTestFreeAligned(void* ptr){
    NWB::Core::Alloc::CoreFreeAligned(ptr, "NWB::Tests::ECS::FreeAligned");
}


struct TestWorld{
    NWB::Core::Alloc::CustomArena arena;
    NWB::Core::Alloc::ThreadPool threadPool;
    NWB::Core::ECS::World world;

    TestWorld()
        : arena(&ECSTestAlloc, &ECSTestFree, &ECSTestAllocAligned, &ECSTestFreeAligned)
        , threadPool(0)
        , world(arena, threadPool)
    {}
};


static void TestProjectAndMainCamera(TestContext& context){
    TestWorld testWorld;

    auto projectEntity = testWorld.world.createEntity();
    auto& project = projectEntity.addComponent<NWB::Core::ECS::ProjectComponent>();
    NWB_ECS_TEST_CHECK(context, project.mainCamera == NWB::Core::ECS::ENTITY_ID_INVALID);

    auto cameraEntity = testWorld.world.createEntity();
    auto& transform = cameraEntity.addComponent<NWB::Core::ECS::TransformComponent>();
    auto& camera = cameraEntity.addComponent<NWB::Core::ECS::CameraComponent>();
    auto& controller = cameraEntity.addComponent<NWB::Core::ECS::FpsCameraControllerComponent>();
    project.mainCamera = cameraEntity.id();

    NWB_ECS_TEST_CHECK(context, projectEntity.hasComponent<NWB::Core::ECS::ProjectComponent>());
    NWB_ECS_TEST_CHECK(context, cameraEntity.hasComponent<NWB::Core::ECS::TransformComponent>());
    NWB_ECS_TEST_CHECK(context, cameraEntity.hasComponent<NWB::Core::ECS::CameraComponent>());
    NWB_ECS_TEST_CHECK(context, cameraEntity.hasComponent<NWB::Core::ECS::FpsCameraControllerComponent>());
    NWB_ECS_TEST_CHECK(context, project.mainCamera == cameraEntity.id());
    NWB_ECS_TEST_CHECK(context, project.mainCamera.valid());
    NWB_ECS_TEST_CHECK(context, testWorld.world.entityCount() == 2);

    NWB_ECS_TEST_CHECK(context, (reinterpret_cast<usize>(&transform) % alignof(NWB::Core::ECS::TransformComponent)) == 0);
    NWB_ECS_TEST_CHECK(context, (reinterpret_cast<usize>(&camera) % alignof(NWB::Core::ECS::CameraComponent)) == 0);
    const usize controllerAlignment = alignof(NWB::Core::ECS::FpsCameraControllerComponent);
    NWB_ECS_TEST_CHECK(context, (reinterpret_cast<usize>(&controller) % controllerAlignment) == 0);
    NWB_ECS_TEST_CHECK(context, transform.position.x == 0.0f);
    NWB_ECS_TEST_CHECK(context, transform.position.y == 0.0f);
    NWB_ECS_TEST_CHECK(context, transform.position.z == 0.0f);
    NWB_ECS_TEST_CHECK(context, transform.rotation.x == 0.0f);
    NWB_ECS_TEST_CHECK(context, transform.rotation.y == 0.0f);
    NWB_ECS_TEST_CHECK(context, transform.rotation.z == 0.0f);
    NWB_ECS_TEST_CHECK(context, transform.rotation.w == 1.0f);
    NWB_ECS_TEST_CHECK(context, transform.scale.x == 1.0f);
    NWB_ECS_TEST_CHECK(context, transform.scale.y == 1.0f);
    NWB_ECS_TEST_CHECK(context, transform.scale.z == 1.0f);
    NWB_ECS_TEST_CHECK(context, camera.verticalFovRadians > 0.0f);
    NWB_ECS_TEST_CHECK(context, camera.nearPlane > 0.0f);
    NWB_ECS_TEST_CHECK(context, camera.farPlane > camera.nearPlane);
    NWB_ECS_TEST_CHECK(context, camera.aspectRatio > 0.0f);
    NWB_ECS_TEST_CHECK(context, controller.yawRadians == 0.0f);
    NWB_ECS_TEST_CHECK(context, controller.pitchRadians == 0.0f);
    NWB_ECS_TEST_CHECK(context, controller.moveSpeed > 0.0f);
    NWB_ECS_TEST_CHECK(context, controller.boostMultiplier >= 1.0f);
    NWB_ECS_TEST_CHECK(context, controller.mouseSensitivityRadiansPerPixel > 0.0f);
    NWB_ECS_TEST_CHECK(context, controller.pitchLimitRadians > 0.0f);

    usize cameraViewCount = 0;
    testWorld.world.view<
        NWB::Core::ECS::TransformComponent,
        NWB::Core::ECS::CameraComponent,
        NWB::Core::ECS::FpsCameraControllerComponent
    >().each(
        [&context, &cameraViewCount, cameraEntityId = cameraEntity.id()](
            NWB::Core::ECS::EntityID entityId,
            NWB::Core::ECS::TransformComponent& viewTransform,
            NWB::Core::ECS::CameraComponent& viewCamera,
            NWB::Core::ECS::FpsCameraControllerComponent& viewController
        ){
            ++cameraViewCount;
            NWB_ECS_TEST_CHECK(context, entityId == cameraEntityId);
            NWB_ECS_TEST_CHECK(context, viewTransform.rotation.w == 1.0f);
            NWB_ECS_TEST_CHECK(context, viewCamera.aspectRatio > 0.0f);
            NWB_ECS_TEST_CHECK(context, viewController.moveSpeed > 0.0f);
        }
    );
    NWB_ECS_TEST_CHECK(context, cameraViewCount == 1);
}

static void TestLightComponents(TestContext& context){
    TestWorld testWorld;

    auto directionalEntity = testWorld.world.createEntity();
    auto& directionalTransform = directionalEntity.addComponent<NWB::Core::ECS::TransformComponent>();
    auto& directionalLight = directionalEntity.addComponent<NWB::Core::ECS::LightComponent>();

    NWB_ECS_TEST_CHECK(context, directionalEntity.hasComponent<NWB::Core::ECS::TransformComponent>());
    NWB_ECS_TEST_CHECK(context, directionalEntity.hasComponent<NWB::Core::ECS::LightComponent>());
    NWB_ECS_TEST_CHECK(context, directionalLight.type == NWB::Core::ECS::LightType::Directional);
    NWB_ECS_TEST_CHECK(context, directionalLight.color.x == 1.0f);
    NWB_ECS_TEST_CHECK(context, directionalLight.color.y == 1.0f);
    NWB_ECS_TEST_CHECK(context, directionalLight.color.z == 1.0f);
    NWB_ECS_TEST_CHECK(context, directionalLight.intensity > 0.0f);
    NWB_ECS_TEST_CHECK(context, directionalLight.range > 0.0f);
    NWB_ECS_TEST_CHECK(context, directionalTransform.rotation.w == 1.0f);

    auto pointEntity = testWorld.world.createEntity();
    auto& pointTransform = pointEntity.addComponent<NWB::Core::ECS::TransformComponent>();
    auto& pointLight = pointEntity.addComponent<NWB::Core::ECS::LightComponent>();
    pointTransform.position = AlignedFloat3Data(1.0f, 2.0f, 3.0f);
    pointLight.type = NWB::Core::ECS::LightType::Point;
    pointLight.color = AlignedFloat3Data(1.0f, 0.75f, 0.5f);
    pointLight.intensity = 4.0f;
    pointLight.range = 12.0f;

    NWB_ECS_TEST_CHECK(context, pointEntity.hasComponent<NWB::Core::ECS::TransformComponent>());
    NWB_ECS_TEST_CHECK(context, pointEntity.hasComponent<NWB::Core::ECS::LightComponent>());
    NWB_ECS_TEST_CHECK(context, pointLight.type == NWB::Core::ECS::LightType::Point);
    NWB_ECS_TEST_CHECK(context, pointLight.color.x == 1.0f);
    NWB_ECS_TEST_CHECK(context, pointLight.color.y == 0.75f);
    NWB_ECS_TEST_CHECK(context, pointLight.color.z == 0.5f);
    NWB_ECS_TEST_CHECK(context, pointLight.intensity == 4.0f);
    NWB_ECS_TEST_CHECK(context, pointLight.range == 12.0f);

    NWB_ECS_TEST_CHECK(context, (reinterpret_cast<usize>(&directionalLight) % alignof(NWB::Core::ECS::LightComponent)) == 0);
    NWB_ECS_TEST_CHECK(context, (reinterpret_cast<usize>(&pointLight) % alignof(NWB::Core::ECS::LightComponent)) == 0);

    usize lightViewCount = 0;
    usize directionalLightCount = 0;
    usize pointLightCount = 0;
    testWorld.world.view<
        NWB::Core::ECS::TransformComponent,
        NWB::Core::ECS::LightComponent
    >().each(
        [&context, &lightViewCount, &directionalLightCount, &pointLightCount, pointEntityId = pointEntity.id()](
            NWB::Core::ECS::EntityID entityId,
            NWB::Core::ECS::TransformComponent& viewTransform,
            NWB::Core::ECS::LightComponent& viewLight
        ){
            ++lightViewCount;
            if(viewLight.type == NWB::Core::ECS::LightType::Directional){
                ++directionalLightCount;
                NWB_ECS_TEST_CHECK(context, viewLight.intensity > 0.0f);
            }
            else if(viewLight.type == NWB::Core::ECS::LightType::Point){
                ++pointLightCount;
                NWB_ECS_TEST_CHECK(context, entityId == pointEntityId);
                NWB_ECS_TEST_CHECK(context, viewTransform.position.x == 1.0f);
                NWB_ECS_TEST_CHECK(context, viewLight.range > 0.0f);
            }
            else{
                NWB_ECS_TEST_CHECK(context, false);
            }
        }
    );
    NWB_ECS_TEST_CHECK(context, lightViewCount == 2);
    NWB_ECS_TEST_CHECK(context, directionalLightCount == 1);
    NWB_ECS_TEST_CHECK(context, pointLightCount == 1);
}

static void TestComponentLifetime(TestContext& context){
    TestWorld testWorld;

    auto entity = testWorld.world.createEntity();
    const auto entityId = entity.id();

    entity.addComponent<NWB::Core::ECS::TransformComponent>();
    NWB_ECS_TEST_CHECK(context, entity.alive());
    NWB_ECS_TEST_CHECK(context, entity.hasComponent<NWB::Core::ECS::TransformComponent>());
    NWB_ECS_TEST_CHECK(context, testWorld.world.tryGetComponent<NWB::Core::ECS::TransformComponent>(entityId) != nullptr);
    NWB_ECS_TEST_CHECK(context, testWorld.world.tryGetComponent<NWB::Core::ECS::CameraComponent>(entityId) == nullptr);

    entity.removeComponent<NWB::Core::ECS::TransformComponent>();
    NWB_ECS_TEST_CHECK(context, !entity.hasComponent<NWB::Core::ECS::TransformComponent>());
    NWB_ECS_TEST_CHECK(context, testWorld.world.tryGetComponent<NWB::Core::ECS::TransformComponent>(entityId) == nullptr);

    entity.addComponent<NWB::Core::ECS::CameraComponent>();
    NWB_ECS_TEST_CHECK(context, entity.hasComponent<NWB::Core::ECS::CameraComponent>());

    entity.addComponent<NWB::Core::ECS::FpsCameraControllerComponent>();
    NWB_ECS_TEST_CHECK(context, entity.hasComponent<NWB::Core::ECS::FpsCameraControllerComponent>());

    entity.addComponent<NWB::Core::ECS::LightComponent>();
    NWB_ECS_TEST_CHECK(context, entity.hasComponent<NWB::Core::ECS::LightComponent>());

    entity.removeComponent<NWB::Core::ECS::LightComponent>();
    NWB_ECS_TEST_CHECK(context, !entity.hasComponent<NWB::Core::ECS::LightComponent>());

    entity.destroy();
    NWB_ECS_TEST_CHECK(context, !entity.alive());
    NWB_ECS_TEST_CHECK(context, testWorld.world.entityCount() == 0);

    auto recycledEntity = testWorld.world.createEntity();
    NWB_ECS_TEST_CHECK(context, recycledEntity.alive());
    NWB_ECS_TEST_CHECK(context, recycledEntity.id() != entityId);
}


#undef NWB_ECS_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int main(){
    NWB::Core::Common::InitializerGuard commonInitializerGuard;
    if(!commonInitializerGuard.initialize()){
        NWB_CERR << "ecs tests failed: common initialization failed\n";
        return 1;
    }

    __hidden_ecs_tests::TestContext context;
    __hidden_ecs_tests::TestProjectAndMainCamera(context);
    __hidden_ecs_tests::TestLightComponents(context);
    __hidden_ecs_tests::TestComponentLifetime(context);

    if(context.failed != 0){
        NWB_CERR << "ecs tests failed: " << context.failed << " of " << (context.passed + context.failed) << '\n';
        return 1;
    }

    NWB_COUT << "ecs tests passed: " << context.passed << '\n';
    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

