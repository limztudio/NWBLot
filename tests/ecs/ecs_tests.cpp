// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/ecs/ecs.h>
#include <core/common/common.h>

#include <tests/test_context.h>

#include <global/compile.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;


#define NWB_ECS_TEST_CHECK(context, expression) (context).checkTrue((expression), #expression, __FILE__, __LINE__)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static usize s_ECSTestAllocationCalls = 0;


static void* ECSTestAlloc(usize size){
    ++s_ECSTestAllocationCalls;
    return NWB::Core::Alloc::CoreAlloc(size, "NWB::Tests::ECS::Alloc");
}

static void ECSTestFree(void* ptr){
    NWB::Core::Alloc::CoreFree(ptr, "NWB::Tests::ECS::Free");
}

static void* ECSTestAllocAligned(usize size, usize align){
    ++s_ECSTestAllocationCalls;
    return NWB::Core::Alloc::CoreAllocAligned(size, align, "NWB::Tests::ECS::AllocAligned");
}

static void ECSTestFreeAligned(void* ptr){
    NWB::Core::Alloc::CoreFreeAligned(ptr, "NWB::Tests::ECS::FreeAligned");
}


using TestWorld = NWB::Tests::EcsTestWorld<&ECSTestAlloc, &ECSTestFree, &ECSTestAllocAligned, &ECSTestFreeAligned>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PositionComponent{
    i32 x = 0;
    i32 y = 0;
};

struct VelocityComponent{
    i32 x = 0;
    i32 y = 0;
};

struct alignas(32) OverAlignedComponent{
    u8 value[32] = {};
};

struct TickMessage{
    u32 value = 0;
};

class CountingSystem final : public NWB::Core::ECS::ISystem{
public:
    explicit CountingSystem(NWB::Core::Alloc::CustomArena& arena)
        : NWB::Core::ECS::ISystem(arena)
    {
        writeAccess<PositionComponent>();
    }

public:
    virtual void update(NWB::Core::ECS::World& world, const f32 delta)override{
        ++updates;
        lastDelta = delta;

        world.view<PositionComponent>().each(
            [](NWB::Core::ECS::EntityID, PositionComponent& position){
                ++position.x;
            }
        );
    }

public:
    u32 updates = 0;
    f32 lastDelta = 0.0f;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestComponentStorageAndView(TestContext& context){
    TestWorld testWorld;

    auto entity = testWorld.world.createEntity();
    const auto entityId = entity.id();
    auto& position = entity.addComponent<PositionComponent>();
    auto& velocity = entity.addComponent<VelocityComponent>();
    auto& aligned = entity.addComponent<OverAlignedComponent>();

    position.x = 2;
    position.y = 4;
    velocity.x = 6;
    velocity.y = 8;

    NWB_ECS_TEST_CHECK(context, entity.alive());
    NWB_ECS_TEST_CHECK(context, testWorld.world.entityCount() == 1);
    NWB_ECS_TEST_CHECK(context, entity.hasComponent<PositionComponent>());
    NWB_ECS_TEST_CHECK(context, entity.hasComponent<VelocityComponent>());
    NWB_ECS_TEST_CHECK(context, entity.hasComponent<OverAlignedComponent>());
    NWB_ECS_TEST_CHECK(context, testWorld.world.tryGetComponent<PositionComponent>(entityId) == &position);
    NWB_ECS_TEST_CHECK(context, testWorld.world.tryGetComponent<VelocityComponent>(entityId) == &velocity);
    NWB_ECS_TEST_CHECK(context, (reinterpret_cast<usize>(&aligned) % alignof(OverAlignedComponent)) == 0);

    usize viewCount = 0;
    testWorld.world.view<PositionComponent, VelocityComponent>().each(
        [&context, &viewCount, entityId](
            NWB::Core::ECS::EntityID viewEntityId,
            PositionComponent& viewPosition,
            VelocityComponent& viewVelocity
        ){
            ++viewCount;
            NWB_ECS_TEST_CHECK(context, viewEntityId == entityId);
            NWB_ECS_TEST_CHECK(context, viewPosition.x == 2);
            NWB_ECS_TEST_CHECK(context, viewPosition.y == 4);
            NWB_ECS_TEST_CHECK(context, viewVelocity.x == 6);
            NWB_ECS_TEST_CHECK(context, viewVelocity.y == 8);
        }
    );
    NWB_ECS_TEST_CHECK(context, viewCount == 1);
}

static void TestEmptyViewDoesNotAllocateComponentPools(TestContext& context){
    TestWorld testWorld;

    const usize allocationCallsBefore = s_ECSTestAllocationCalls;
    usize singleViewCount = 0;
    usize multiViewCount = 0;

    testWorld.world.view<PositionComponent>().each(
        [&singleViewCount](NWB::Core::ECS::EntityID, PositionComponent&){
            ++singleViewCount;
        }
    );
    testWorld.world.view<PositionComponent, VelocityComponent>().each(
        [&multiViewCount](NWB::Core::ECS::EntityID, PositionComponent&, VelocityComponent&){
            ++multiViewCount;
        }
    );

    NWB_ECS_TEST_CHECK(context, singleViewCount == 0);
    NWB_ECS_TEST_CHECK(context, multiViewCount == 0);
    NWB_ECS_TEST_CHECK(context, s_ECSTestAllocationCalls == allocationCallsBefore);
}

static void TestComponentLifetime(TestContext& context){
    TestWorld testWorld;

    auto entity = testWorld.world.createEntity();
    const auto entityId = entity.id();

    entity.addComponent<PositionComponent>();
    NWB_ECS_TEST_CHECK(context, entity.alive());
    NWB_ECS_TEST_CHECK(context, entity.hasComponent<PositionComponent>());
    NWB_ECS_TEST_CHECK(context, testWorld.world.tryGetComponent<PositionComponent>(entityId) != nullptr);
    NWB_ECS_TEST_CHECK(context, testWorld.world.tryGetComponent<VelocityComponent>(entityId) == nullptr);

    entity.removeComponent<PositionComponent>();
    NWB_ECS_TEST_CHECK(context, !entity.hasComponent<PositionComponent>());
    NWB_ECS_TEST_CHECK(context, testWorld.world.tryGetComponent<PositionComponent>(entityId) == nullptr);

    entity.addComponent<VelocityComponent>();
    NWB_ECS_TEST_CHECK(context, entity.hasComponent<VelocityComponent>());

    entity.addComponent<OverAlignedComponent>();
    NWB_ECS_TEST_CHECK(context, entity.hasComponent<OverAlignedComponent>());

    entity.removeComponent<OverAlignedComponent>();
    NWB_ECS_TEST_CHECK(context, !entity.hasComponent<OverAlignedComponent>());

    entity.destroy();
    NWB_ECS_TEST_CHECK(context, !entity.alive());
    NWB_ECS_TEST_CHECK(context, testWorld.world.entityCount() == 0);

    auto recycledEntity = testWorld.world.createEntity();
    NWB_ECS_TEST_CHECK(context, recycledEntity.alive());
    NWB_ECS_TEST_CHECK(context, recycledEntity.id() != entityId);
}

static void TestMessageBus(TestContext& context){
    TestWorld testWorld;

    TickMessage lvalueMessage{ 7u };
    testWorld.world.postMessage(lvalueMessage);
    testWorld.world.postMessage(TickMessage{ 11u });
    NWB_ECS_TEST_CHECK(context, testWorld.world.messageCount<TickMessage>() == 0);

    testWorld.world.swapMessageBuffers();
    NWB_ECS_TEST_CHECK(context, testWorld.world.messageCount<TickMessage>() == 2);

    u32 consumedCount = 0;
    u32 consumedValueSum = 0;
    testWorld.world.consumeMessages<TickMessage>(
        [&consumedCount, &consumedValueSum](const TickMessage& message){
            ++consumedCount;
            consumedValueSum += message.value;
        }
    );
    NWB_ECS_TEST_CHECK(context, consumedCount == 2);
    NWB_ECS_TEST_CHECK(context, consumedValueSum == 18u);

    testWorld.world.clearMessages();
    NWB_ECS_TEST_CHECK(context, testWorld.world.messageCount<TickMessage>() == 0);
}

static void TestSystemTick(TestContext& context){
    TestWorld testWorld;

    auto entity = testWorld.world.createEntity();
    auto& position = entity.addComponent<PositionComponent>();
    position.x = 4;

    auto& system = testWorld.world.addSystem<CountingSystem>();
    NWB_ECS_TEST_CHECK(context, testWorld.world.getSystem<CountingSystem>() == &system);

    testWorld.world.tick(0.25f);
    NWB_ECS_TEST_CHECK(context, system.updates == 1);
    NWB_ECS_TEST_CHECK(context, system.lastDelta == 0.25f);
    NWB_ECS_TEST_CHECK(context, position.x == 5);

    testWorld.world.removeSystem(system);
    NWB_ECS_TEST_CHECK(context, testWorld.world.getSystem<CountingSystem>() == nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_ECS_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int EntryPoint(const isize argc, tchar** argv, void*){
    static_cast<void>(argc);
    static_cast<void>(argv);

    return NWB::Tests::RunTestSuite("ecs", [](NWB::Tests::TestContext& context){
        __hidden_ecs_tests::TestComponentStorageAndView(context);
        __hidden_ecs_tests::TestEmptyViewDoesNotAllocateComponentPools(context);
        __hidden_ecs_tests::TestComponentLifetime(context);
        __hidden_ecs_tests::TestMessageBus(context);
        __hidden_ecs_tests::TestSystemTick(context);
    });
}


#include <core/common/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

