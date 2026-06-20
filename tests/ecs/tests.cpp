// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/ecs/module.h>
#include <core/common/module.h>

#include <tests/ecs_test_world.h>
#include <gtest/gtest.h>

#include <global/compile.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestWorld = NWB::Tests::EcsTestWorld;


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

struct MoveOnlyMessage{
    explicit MoveOnlyMessage(u32 v)
        : value(v)
    {}
    MoveOnlyMessage(const MoveOnlyMessage&) = delete;
    MoveOnlyMessage& operator=(const MoveOnlyMessage&) = delete;
    MoveOnlyMessage(MoveOnlyMessage&& rhs)noexcept
        : value(rhs.value)
    {
        rhs.value = 0u;
    }
    MoveOnlyMessage& operator=(MoveOnlyMessage&& rhs)noexcept{
        if(this != &rhs){
            value = rhs.value;
            rhs.value = 0u;
        }
        return *this;
    }

    u32 value = 0u;
};

class CountingSystem final : public NWB::Core::ECS::ISystem{
public:
    explicit CountingSystem(NWB::Core::Alloc::GlobalArena& arena)
        : NWB::Core::ECS::ISystem(arena)
    {
        writeAccess<PositionComponent>();
    }

public:
    virtual void prepare(NWB::Core::ECS::World& world)override{
        static_cast<void>(world);
        ++prepares;
    }

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
    u32 prepares = 0;
    u32 updates = 0;
    f32 lastDelta = 0.0f;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestComponentStorageAndView(){
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

    EXPECT_TRUE((entity.alive()));
    EXPECT_TRUE((testWorld.world.entityCount() == 1));
    EXPECT_TRUE((entity.hasComponent<PositionComponent>()));
    EXPECT_TRUE((entity.hasComponent<VelocityComponent>()));
    EXPECT_TRUE((entity.hasComponent<OverAlignedComponent>()));
    EXPECT_TRUE((testWorld.world.tryGetComponent<PositionComponent>(entityId) == &position));
    EXPECT_TRUE((testWorld.world.tryGetComponent<VelocityComponent>(entityId) == &velocity));
    EXPECT_TRUE(((reinterpret_cast<usize>(&aligned) % alignof(OverAlignedComponent)) == 0));

    usize viewCount = 0;
    testWorld.world.view<PositionComponent, VelocityComponent>().each(
        [&viewCount, entityId](
            NWB::Core::ECS::EntityID viewEntityId,
            PositionComponent& viewPosition,
            VelocityComponent& viewVelocity
        ){
            ++viewCount;
            EXPECT_TRUE((viewEntityId == entityId));
            EXPECT_TRUE((viewPosition.x == 2));
            EXPECT_TRUE((viewPosition.y == 4));
            EXPECT_TRUE((viewVelocity.x == 6));
            EXPECT_TRUE((viewVelocity.y == 8));
        }
    );
    EXPECT_TRUE((viewCount == 1));
}

static void TestEmptyViewDoesNotAllocateComponentPools(){
    TestWorld testWorld;

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

    EXPECT_TRUE((singleViewCount == 0));
    EXPECT_TRUE((multiViewCount == 0));
}

static void TestComponentLifetime(){
    TestWorld testWorld;

    auto entity = testWorld.world.createEntity();
    const auto entityId = entity.id();

    entity.addComponent<PositionComponent>();
    EXPECT_TRUE((entity.alive()));
    EXPECT_TRUE((entity.hasComponent<PositionComponent>()));
    EXPECT_TRUE((testWorld.world.tryGetComponent<PositionComponent>(entityId) != nullptr));
    EXPECT_TRUE((testWorld.world.tryGetComponent<VelocityComponent>(entityId) == nullptr));

    entity.removeComponent<PositionComponent>();
    EXPECT_TRUE((!entity.hasComponent<PositionComponent>()));
    EXPECT_TRUE((testWorld.world.tryGetComponent<PositionComponent>(entityId) == nullptr));

    entity.addComponent<VelocityComponent>();
    EXPECT_TRUE((entity.hasComponent<VelocityComponent>()));

    entity.addComponent<OverAlignedComponent>();
    EXPECT_TRUE((entity.hasComponent<OverAlignedComponent>()));

    entity.removeComponent<OverAlignedComponent>();
    EXPECT_TRUE((!entity.hasComponent<OverAlignedComponent>()));

    entity.destroy();
    EXPECT_TRUE((!entity.alive()));
    EXPECT_TRUE((testWorld.world.entityCount() == 0));

    auto recycledEntity = testWorld.world.createEntity();
    EXPECT_TRUE((recycledEntity.alive()));
    EXPECT_TRUE((recycledEntity.id() != entityId));
}

static void TestComponentMutationVersion(){
    TestWorld testWorld;

    EXPECT_TRUE((testWorld.world.componentMutationVersion<PositionComponent>() == 0u));

    auto entity = testWorld.world.createEntity();
    entity.addComponent<PositionComponent>();
    EXPECT_TRUE((testWorld.world.componentMutationVersion<PositionComponent>() == 1u));

    entity.addComponent<PositionComponent>();
    EXPECT_TRUE((testWorld.world.componentMutationVersion<PositionComponent>() == 1u));

    entity.addComponent<VelocityComponent>();
    EXPECT_TRUE((testWorld.world.componentMutationVersion<PositionComponent>() == 1u));
    EXPECT_TRUE((testWorld.world.componentMutationVersion<VelocityComponent>() == 1u));

    entity.removeComponent<VelocityComponent>();
    EXPECT_TRUE((testWorld.world.componentMutationVersion<VelocityComponent>() == 2u));

    entity.removeComponent<VelocityComponent>();
    EXPECT_TRUE((testWorld.world.componentMutationVersion<VelocityComponent>() == 2u));

    entity.destroy();
    EXPECT_TRUE((testWorld.world.componentMutationVersion<PositionComponent>() == 2u));
}

static void TestMessageBus(){
    TestWorld testWorld;

    TickMessage lvalueMessage{ 7u };
    testWorld.world.postMessage(lvalueMessage);
    testWorld.world.postMessage(TickMessage{ 11u });
    EXPECT_TRUE((testWorld.world.messageCount<TickMessage>() == 0));

    testWorld.world.swapMessageBuffers();
    EXPECT_TRUE((testWorld.world.messageCount<TickMessage>() == 2));

    u32 consumedCount = 0;
    u32 consumedValueSum = 0;
    testWorld.world.consumeMessages<TickMessage>(
        [&consumedCount, &consumedValueSum](const TickMessage& message){
            ++consumedCount;
            consumedValueSum += message.value;
        }
    );
    EXPECT_TRUE((consumedCount == 2));
    EXPECT_TRUE((consumedValueSum == 18u));

    testWorld.world.clearMessages();
    EXPECT_TRUE((testWorld.world.messageCount<TickMessage>() == 0));
}

static void TestMoveOnlyMessageBus(){
    TestWorld testWorld;

    testWorld.world.emplaceMessage<MoveOnlyMessage>(23u);
    EXPECT_TRUE((testWorld.world.messageCount<MoveOnlyMessage>() == 0));

    testWorld.world.swapMessageBuffers();
    EXPECT_TRUE((testWorld.world.messageCount<MoveOnlyMessage>() == 1));

    u32 consumedCount = 0u;
    u32 consumedValue = 0u;
    testWorld.world.consumeMessages<MoveOnlyMessage>(
        [&consumedCount, &consumedValue](const MoveOnlyMessage& message){
            ++consumedCount;
            consumedValue = message.value;
        }
    );
    EXPECT_TRUE((consumedCount == 1u));
    EXPECT_TRUE((consumedValue == 23u));

    testWorld.world.clearMessages();
    EXPECT_TRUE((testWorld.world.messageCount<MoveOnlyMessage>() == 0));

    testWorld.world.emplaceMessage<MoveOnlyMessage>(41u);
    testWorld.world.clearMessages();
    testWorld.world.swapMessageBuffers();
    EXPECT_TRUE((testWorld.world.messageCount<MoveOnlyMessage>() == 0));
}

static void TestSystemTick(){
    TestWorld testWorld;

    auto entity = testWorld.world.createEntity();
    auto& position = entity.addComponent<PositionComponent>();
    position.x = 4;

    auto& system = testWorld.world.addSystem<CountingSystem>();
    EXPECT_TRUE((testWorld.world.getSystem<CountingSystem>() == &system));

    testWorld.world.tick(0.25f);
    EXPECT_TRUE((system.prepares == 1));
    EXPECT_TRUE((system.updates == 1));
    EXPECT_TRUE((system.lastDelta == 0.25f));
    EXPECT_TRUE((position.x == 5));

    testWorld.world.removeSystem(system);
    EXPECT_TRUE((testWorld.world.getSystem<CountingSystem>() == nullptr));
}

static void TestDuplicateComponentAddIsStable(){
    TestWorld testWorld;

    auto entity = testWorld.world.createEntity();
    auto& first = entity.addComponent<PositionComponent>();
    first.x = 9;
    first.y = 4;

    auto& second = entity.addComponent<PositionComponent>();
    EXPECT_TRUE((&first == &second));
    EXPECT_TRUE((second.x == 9));
    EXPECT_TRUE((second.y == 4));

    usize viewCount = 0u;
    testWorld.world.view<PositionComponent>().each(
        [&viewCount](NWB::Core::ECS::EntityID, PositionComponent&){
            ++viewCount;
        }
    );
    EXPECT_TRUE((viewCount == 1u));

    entity.removeComponent<PositionComponent>();
    EXPECT_TRUE((!entity.hasComponent<PositionComponent>()));
}

static void TestDuplicateSchedulerAddIsStable(){
    TestWorld testWorld;

    auto entity = testWorld.world.createEntity();
    auto& position = entity.addComponent<PositionComponent>();
    position.x = 1;

    CountingSystem system(testWorld.arena);
    NWB::Core::ECS::SystemScheduler scheduler(testWorld.arena);
    scheduler.addSystem(system);
    scheduler.addSystem(system);
    scheduler.execute(testWorld.world, 0.5f);

    EXPECT_TRUE((system.prepares == 1u));
    EXPECT_TRUE((system.updates == 1u));
    EXPECT_TRUE((position.x == 2));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Ecs, ComponentStorageAndView){
    __hidden_tests::TestComponentStorageAndView();
}

TEST(Ecs, EmptyViewDoesNotAllocateComponentPools){
    __hidden_tests::TestEmptyViewDoesNotAllocateComponentPools();
}

TEST(Ecs, ComponentLifetime){
    __hidden_tests::TestComponentLifetime();
}

TEST(Ecs, ComponentMutationVersion){
    __hidden_tests::TestComponentMutationVersion();
}

TEST(Ecs, MessageBus){
    __hidden_tests::TestMessageBus();
}

TEST(Ecs, MoveOnlyMessageBus){
    __hidden_tests::TestMoveOnlyMessageBus();
}

TEST(Ecs, SystemTick){
    __hidden_tests::TestSystemTick();
}

TEST(Ecs, DuplicateComponentAddIsStable){
    __hidden_tests::TestDuplicateComponentAddIsStable();
}

TEST(Ecs, DuplicateSchedulerAddIsStable){
    __hidden_tests::TestDuplicateSchedulerAddIsStable();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

