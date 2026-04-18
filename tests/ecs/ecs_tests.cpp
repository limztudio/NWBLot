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
    project.mainCamera = cameraEntity.id();

    NWB_ECS_TEST_CHECK(context, projectEntity.hasComponent<NWB::Core::ECS::ProjectComponent>());
    NWB_ECS_TEST_CHECK(context, cameraEntity.hasComponent<NWB::Core::ECS::TransformComponent>());
    NWB_ECS_TEST_CHECK(context, cameraEntity.hasComponent<NWB::Core::ECS::CameraComponent>());
    NWB_ECS_TEST_CHECK(context, project.mainCamera == cameraEntity.id());
    NWB_ECS_TEST_CHECK(context, project.mainCamera.valid());
    NWB_ECS_TEST_CHECK(context, testWorld.world.entityCount() == 2);

    NWB_ECS_TEST_CHECK(context, (reinterpret_cast<usize>(&transform) % alignof(NWB::Core::ECS::TransformComponent)) == 0);
    NWB_ECS_TEST_CHECK(context, (reinterpret_cast<usize>(&camera) % alignof(NWB::Core::ECS::CameraComponent)) == 0);
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

    usize cameraViewCount = 0;
    testWorld.world.view<NWB::Core::ECS::TransformComponent, NWB::Core::ECS::CameraComponent>().each(
        [&context, &cameraViewCount, cameraEntityId = cameraEntity.id()](
            NWB::Core::ECS::EntityID entityId,
            NWB::Core::ECS::TransformComponent& viewTransform,
            NWB::Core::ECS::CameraComponent& viewCamera
        ){
            ++cameraViewCount;
            NWB_ECS_TEST_CHECK(context, entityId == cameraEntityId);
            NWB_ECS_TEST_CHECK(context, viewTransform.rotation.w == 1.0f);
            NWB_ECS_TEST_CHECK(context, viewCamera.aspectRatio > 0.0f);
        }
    );
    NWB_ECS_TEST_CHECK(context, cameraViewCount == 1);
}

static void TestComponentLifetime(TestContext& context){
    TestWorld testWorld;

    auto entity = testWorld.world.createEntity();
    const auto entityId = entity.id();

    entity.addComponent<NWB::Core::ECS::TransformComponent>();
    NWB_ECS_TEST_CHECK(context, entity.alive());
    NWB_ECS_TEST_CHECK(context, entity.hasComponent<NWB::Core::ECS::TransformComponent>());

    entity.removeComponent<NWB::Core::ECS::TransformComponent>();
    NWB_ECS_TEST_CHECK(context, !entity.hasComponent<NWB::Core::ECS::TransformComponent>());

    entity.addComponent<NWB::Core::ECS::CameraComponent>();
    NWB_ECS_TEST_CHECK(context, entity.hasComponent<NWB::Core::ECS::CameraComponent>());

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
    __hidden_ecs_tests::TestComponentLifetime(context);

    if(context.failed != 0){
        NWB_CERR << "ecs tests failed: " << context.failed << " of " << (context.passed + context.failed) << '\n';
        return 1;
    }

    NWB_COUT << "ecs tests passed: " << context.passed << '\n';
    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

