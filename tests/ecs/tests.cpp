#include <core/ecs/module.h>
#include <core/common/module.h>

#include <tests/ecs_test_world.h>
#include <gtest/gtest.h>

#include <global/atomic.h>
#include <global/compile.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestWorld = NWB::Tests::EcsTestWorld;

inline constexpr Name s_EcsParallelTestArena("tests/ecs_parallel");


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


TEST(Ecs, ComponentStorageAndView){
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

    EXPECT_TRUE(entity.alive());
    EXPECT_EQ(testWorld.world.entityCount(), 1u);
    EXPECT_TRUE(entity.hasComponent<PositionComponent>());
    EXPECT_TRUE(entity.hasComponent<VelocityComponent>());
    EXPECT_TRUE(entity.hasComponent<OverAlignedComponent>());
    EXPECT_EQ(testWorld.world.tryGetComponent<PositionComponent>(entityId), &position);
    EXPECT_EQ(testWorld.world.tryGetComponent<VelocityComponent>(entityId), &velocity);
    EXPECT_EQ((reinterpret_cast<usize>(&aligned) % alignof(OverAlignedComponent)), 0u);

    usize viewCount = 0;
    testWorld.world.view<PositionComponent, VelocityComponent>().each(
        [&viewCount, entityId](
            NWB::Core::ECS::EntityID viewEntityId,
            PositionComponent& viewPosition,
            VelocityComponent& viewVelocity
        ){
            ++viewCount;
            EXPECT_EQ(viewEntityId, entityId);
            EXPECT_EQ(viewPosition.x, 2);
            EXPECT_EQ(viewPosition.y, 4);
            EXPECT_EQ(viewVelocity.x, 6);
            EXPECT_EQ(viewVelocity.y, 8);
        }
    );
    EXPECT_EQ(viewCount, 1u);
}

TEST(Ecs, EmptyViewDoesNotAllocateComponentPools){
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

    EXPECT_EQ(singleViewCount, 0u);
    EXPECT_EQ(multiViewCount, 0u);
}

TEST(Ecs, ComponentLifetime){
    TestWorld testWorld;

    auto entity = testWorld.world.createEntity();
    const auto entityId = entity.id();

    entity.addComponent<PositionComponent>();
    EXPECT_TRUE(entity.alive());
    EXPECT_TRUE(entity.hasComponent<PositionComponent>());
    EXPECT_NE(testWorld.world.tryGetComponent<PositionComponent>(entityId), nullptr);
    EXPECT_EQ(testWorld.world.tryGetComponent<VelocityComponent>(entityId), nullptr);

    entity.removeComponent<PositionComponent>();
    EXPECT_FALSE(entity.hasComponent<PositionComponent>());
    EXPECT_EQ(testWorld.world.tryGetComponent<PositionComponent>(entityId), nullptr);

    entity.addComponent<VelocityComponent>();
    EXPECT_TRUE(entity.hasComponent<VelocityComponent>());

    entity.addComponent<OverAlignedComponent>();
    EXPECT_TRUE(entity.hasComponent<OverAlignedComponent>());

    entity.removeComponent<OverAlignedComponent>();
    EXPECT_FALSE(entity.hasComponent<OverAlignedComponent>());

    entity.destroy();
    EXPECT_FALSE(entity.alive());
    EXPECT_EQ(testWorld.world.entityCount(), 0u);

    auto recycledEntity = testWorld.world.createEntity();
    EXPECT_TRUE(recycledEntity.alive());
    EXPECT_NE(recycledEntity.id(), entityId);
}

TEST(Ecs, ComponentMutationVersion){
    TestWorld testWorld;

    EXPECT_EQ(testWorld.world.componentMutationVersion<PositionComponent>(), 0u);

    auto entity = testWorld.world.createEntity();
    entity.addComponent<PositionComponent>();
    EXPECT_EQ(testWorld.world.componentMutationVersion<PositionComponent>(), 1u);

    entity.addComponent<PositionComponent>();
    EXPECT_EQ(testWorld.world.componentMutationVersion<PositionComponent>(), 1u);

    entity.addComponent<VelocityComponent>();
    EXPECT_EQ(testWorld.world.componentMutationVersion<PositionComponent>(), 1u);
    EXPECT_EQ(testWorld.world.componentMutationVersion<VelocityComponent>(), 1u);

    entity.removeComponent<VelocityComponent>();
    EXPECT_EQ(testWorld.world.componentMutationVersion<VelocityComponent>(), 2u);

    entity.removeComponent<VelocityComponent>();
    EXPECT_EQ(testWorld.world.componentMutationVersion<VelocityComponent>(), 2u);

    entity.destroy();
    EXPECT_EQ(testWorld.world.componentMutationVersion<PositionComponent>(), 2u);
}

TEST(Ecs, MessageBus){
    TestWorld testWorld;

    TickMessage lvalueMessage{ 7u };
    testWorld.world.postMessage(lvalueMessage);
    testWorld.world.postMessage(TickMessage{ 11u });
    EXPECT_EQ(testWorld.world.messageCount<TickMessage>(), 0u);

    testWorld.world.swapMessageBuffers();
    EXPECT_EQ(testWorld.world.messageCount<TickMessage>(), 2u);

    u32 consumedCount = 0;
    u32 consumedValueSum = 0;
    testWorld.world.consumeMessages<TickMessage>(
        [&consumedCount, &consumedValueSum](const TickMessage& message){
            ++consumedCount;
            consumedValueSum += message.value;
        }
    );
    EXPECT_EQ(consumedCount, 2u);
    EXPECT_EQ(consumedValueSum, 18u);

    testWorld.world.clearMessages();
    EXPECT_EQ(testWorld.world.messageCount<TickMessage>(), 0u);
}

TEST(Ecs, MoveOnlyMessageBus){
    TestWorld testWorld;

    testWorld.world.emplaceMessage<MoveOnlyMessage>(23u);
    EXPECT_EQ(testWorld.world.messageCount<MoveOnlyMessage>(), 0u);

    testWorld.world.swapMessageBuffers();
    EXPECT_EQ(testWorld.world.messageCount<MoveOnlyMessage>(), 1u);

    u32 consumedCount = 0u;
    u32 consumedValue = 0u;
    testWorld.world.consumeMessages<MoveOnlyMessage>(
        [&consumedCount, &consumedValue](const MoveOnlyMessage& message){
            ++consumedCount;
            consumedValue = message.value;
        }
    );
    EXPECT_EQ(consumedCount, 1u);
    EXPECT_EQ(consumedValue, 23u);

    testWorld.world.clearMessages();
    EXPECT_EQ(testWorld.world.messageCount<MoveOnlyMessage>(), 0u);

    testWorld.world.emplaceMessage<MoveOnlyMessage>(41u);
    testWorld.world.clearMessages();
    testWorld.world.swapMessageBuffers();
    EXPECT_EQ(testWorld.world.messageCount<MoveOnlyMessage>(), 0u);
}

TEST(Ecs, SystemTick){
    TestWorld testWorld;

    auto entity = testWorld.world.createEntity();
    auto& position = entity.addComponent<PositionComponent>();
    position.x = 4;

    auto& system = testWorld.world.addSystem<CountingSystem>();
    EXPECT_EQ(testWorld.world.getSystem<CountingSystem>(), &system);

    testWorld.world.tick(0.25f);
    EXPECT_EQ(system.prepares, 1u);
    EXPECT_EQ(system.updates, 1u);
    EXPECT_EQ(system.lastDelta, 0.25f);
    EXPECT_EQ(position.x, 5);

    testWorld.world.removeSystem(system);
    EXPECT_EQ(testWorld.world.getSystem<CountingSystem>(), nullptr);
}

TEST(Ecs, DuplicateComponentAddIsStable){
    TestWorld testWorld;

    auto entity = testWorld.world.createEntity();
    auto& first = entity.addComponent<PositionComponent>();
    first.x = 9;
    first.y = 4;

    auto& second = entity.addComponent<PositionComponent>();
    EXPECT_EQ(&first, &second);
    EXPECT_EQ(second.x, 9);
    EXPECT_EQ(second.y, 4);

    usize viewCount = 0u;
    testWorld.world.view<PositionComponent>().each(
        [&viewCount](NWB::Core::ECS::EntityID, PositionComponent&){
            ++viewCount;
        }
    );
    EXPECT_EQ(viewCount, 1u);

    entity.removeComponent<PositionComponent>();
    EXPECT_FALSE(entity.hasComponent<PositionComponent>());
}

TEST(Ecs, ParallelEachVisitsSingleAndMultiComponentViews){
    NWB::Core::Alloc::GlobalArena arena(s_EcsParallelTestArena);
    NWB::Core::Alloc::ThreadPool threadPool(3u, NWB::Core::Alloc::CoreAffinity::Any);
    NWB::Core::ECS::World world(arena, threadPool);

    static constexpr u32 s_EntityCount = 512u;
    for(u32 i = 0u; i < s_EntityCount; ++i){
        auto entity = world.createEntity();
        auto& position = entity.addComponent<PositionComponent>();
        position.x = static_cast<i32>(i);

        if((i & 1u) == 0u){
            auto& velocity = entity.addComponent<VelocityComponent>();
            velocity.x = static_cast<i32>(i * 2u);
        }
    }

    Atomic<u32> positionVisits{ 0u };
    world.view<PositionComponent>().parallelEach(
        threadPool,
        32u,
        [&positionVisits](NWB::Core::ECS::EntityID, PositionComponent& position){
            position.y = position.x + 1;
            positionVisits.fetch_add(1u, MemoryOrder::relaxed);
        }
    );

    EXPECT_EQ(positionVisits.load(MemoryOrder::relaxed), s_EntityCount);

    u32 verifiedPositions = 0u;
    world.view<PositionComponent>().each(
        [&verifiedPositions](NWB::Core::ECS::EntityID, PositionComponent& position){
            EXPECT_EQ(position.y, position.x + 1);
            ++verifiedPositions;
        }
    );
    EXPECT_EQ(verifiedPositions, s_EntityCount);

    Atomic<u32> pairVisits{ 0u };
    world.view<PositionComponent, VelocityComponent>().parallelEach(
        threadPool,
        16u,
        [&pairVisits](NWB::Core::ECS::EntityID, PositionComponent& position, VelocityComponent& velocity){
            position.x += velocity.x;
            pairVisits.fetch_add(1u, MemoryOrder::relaxed);
        }
    );

    EXPECT_EQ(pairVisits.load(MemoryOrder::relaxed), s_EntityCount / 2u);
}

TEST(Ecs, ParallelEachNestedInParallelForFallsBackSerial){
    NWB::Core::Alloc::GlobalArena arena(s_EcsParallelTestArena);
    NWB::Core::Alloc::ThreadPool threadPool(3u, NWB::Core::Alloc::CoreAffinity::Any);
    NWB::Core::ECS::World world(arena, threadPool);

    static constexpr u32 s_EntityCount = 64u;
    static constexpr usize s_OuterCount = 4u;
    for(u32 i = 0u; i < s_EntityCount; ++i){
        auto entity = world.createEntity();
        auto& position = entity.addComponent<PositionComponent>();
        position.x = static_cast<i32>(i);
    }

    Atomic<u32> visits{ 0u };
    threadPool.parallelFor(
        static_cast<usize>(0),
        s_OuterCount,
        [&](usize outerIndex){
            static_cast<void>(outerIndex);
            world.view<PositionComponent>().parallelEach(
                threadPool,
                1u,
                [&visits](NWB::Core::ECS::EntityID, PositionComponent& position){
                    static_cast<void>(position);
                    visits.fetch_add(1u, MemoryOrder::relaxed);
                }
            );
        }
    );

    EXPECT_EQ(visits.load(MemoryOrder::relaxed), s_EntityCount * static_cast<u32>(s_OuterCount));
}

TEST(Ecs, DuplicateSchedulerAddIsStable){
    TestWorld testWorld;

    auto entity = testWorld.world.createEntity();
    auto& position = entity.addComponent<PositionComponent>();
    position.x = 1;

    CountingSystem system(testWorld.arena);
    NWB::Core::ECS::SystemScheduler scheduler(testWorld.arena);
    scheduler.addSystem(system);
    scheduler.addSystem(system);
    scheduler.execute(testWorld.world, 0.5f);

    EXPECT_EQ(system.prepares, 1u);
    EXPECT_EQ(system.updates, 1u);
    EXPECT_EQ(position.x, 2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

