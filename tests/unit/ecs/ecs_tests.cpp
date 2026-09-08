// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/ecs/module.h>
#include <core/common/module.h>

#include <tests/common/ecs_test_world.h>
#include <gtest/gtest.h>

#include <global/atomic.h>
#include <global/compile.h>
#include <global/timer.h>


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

template<usize I>
struct PoolSlotComponent{
    usize value = I;
};

template<usize... Is>
Array<usize, sizeof...(Is)> RegisterPoolSlotTypes(IndexSequence<Is...>){
    return { NWB::Core::ECS::ComponentType<PoolSlotComponent<Is>>()... };
}

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


class ScheduledSystem final : public NWB::Core::ECS::ISystem{
public:
    ScheduledSystem(
        NWB::Core::Alloc::GlobalArena& arena,
        InitializerList<NWB::Core::ECS::ComponentAccess> accesses,
        Function<void()> update,
        NWB::Core::Alloc::CpuTaskOptions options = {}
    )
        : NWB::Core::ECS::ISystem(arena)
        , m_update(Move(update))
        , m_options(options)
    {
        for(const auto& access : accesses)
            registerAccess(access.typeId, access.mode);
    }


public:
    virtual void update(NWB::Core::ECS::World& world, f32 delta)override{
        static_cast<void>(world);
        static_cast<void>(delta);
        m_update();
    }

    [[nodiscard]] virtual NWB::Core::Alloc::CpuTaskOptions taskOptions()const override{ return m_options; }


private:
    Function<void()> m_update;
    NWB::Core::Alloc::CpuTaskOptions m_options;
};


static thread_local bool s_EcsCallerThread = false;

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

TEST(Ecs, EntityFacadeRehydratesEntityId){
    TestWorld testWorld;

    const auto entityId = testWorld.world.createEntity().id();
    auto entity = testWorld.world.entity(entityId);
    auto& position = entity.addComponent<PositionComponent>();
    position.x = 17;

    EXPECT_TRUE(entity.hasComponent<PositionComponent>());
    EXPECT_EQ(entity.getComponent<PositionComponent>().x, 17);

    entity.removeComponent<PositionComponent>();
    EXPECT_FALSE(entity.hasComponent<PositionComponent>());
}

TEST(Ecs, EmptyViewDoesNotAllocateComponentPools){
    TestWorld testWorld;
    const auto initialMemory = testWorld.arena.memoryStats();

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
    testWorld.world.view<PositionComponent>().parallelEach(
        testWorld.world.taskScope(),
        [&singleViewCount](NWB::Core::ECS::EntityID, PositionComponent&){
            ++singleViewCount;
        }
    );
    testWorld.world.view<PositionComponent, VelocityComponent>().parallelEach(
        testWorld.world.taskScope(),
        [&multiViewCount](NWB::Core::ECS::EntityID, PositionComponent&, VelocityComponent&){
            ++multiViewCount;
        }
    );

    EXPECT_EQ(singleViewCount, 0u);
    EXPECT_EQ(multiViewCount, 0u);

    const auto finalMemory = testWorld.arena.memoryStats();
    EXPECT_EQ(finalMemory.allocationCount, initialMemory.allocationCount);
    EXPECT_EQ(finalMemory.reallocationCount, initialMemory.reallocationCount);
    EXPECT_EQ(finalMemory.usedBytes, initialMemory.usedBytes);
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

TEST(Ecs, ComponentPoolsPreserveSparseTypeLookupAndWorldIsolation){
    const auto typeIds = RegisterPoolSlotTypes(IndexSequence<
        0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u,
        16u, 17u, 18u, 19u, 20u, 21u, 22u, 23u, 24u, 25u, 26u, 27u, 28u, 29u, 30u, 31u,
        32u, 33u, 34u, 35u, 36u, 37u, 38u, 39u, 40u, 41u, 42u, 43u, 44u, 45u, 46u, 47u,
        48u, 49u, 50u, 51u, 52u, 53u, 54u, 55u, 56u, 57u, 58u, 59u, 60u, 61u, 62u, 63u
    >{});
    EXPECT_NE(typeIds.front(), typeIds.back());

    TestWorld firstWorld;
    TestWorld secondWorld;
    auto firstEntity = firstWorld.world.createEntity();
    auto secondEntity = secondWorld.world.createEntity();
    auto& firstComponent = firstEntity.addComponent<PoolSlotComponent<0u>>();
    firstComponent.value = 17u;
    const auto firstView = firstWorld.world.view<PoolSlotComponent<0u>>();

    secondEntity.addComponent<PoolSlotComponent<31u>>().value = 29u;
    firstEntity.addComponent<PoolSlotComponent<64u>>().value = 41u;

    EXPECT_EQ(firstWorld.world.tryGetComponent<PoolSlotComponent<0u>>(firstEntity.id()), &firstComponent);
    EXPECT_EQ(firstWorld.world.tryGetComponent<PoolSlotComponent<31u>>(firstEntity.id()), nullptr);
    EXPECT_EQ(secondWorld.world.tryGetComponent<PoolSlotComponent<0u>>(secondEntity.id()), nullptr);
    EXPECT_EQ(secondWorld.world.tryGetComponent<PoolSlotComponent<64u>>(secondEntity.id()), nullptr);
    EXPECT_EQ(firstWorld.world.componentMutationVersion<PoolSlotComponent<31u>>(), 0u);

    const NWB::Core::ECS::World& constFirstWorld = firstWorld.world;
    EXPECT_EQ(constFirstWorld.tryGetComponent<PoolSlotComponent<0u>>(firstEntity.id()), &firstComponent);
    EXPECT_EQ(constFirstWorld.tryGetComponent<PoolSlotComponent<31u>>(firstEntity.id()), nullptr);
    EXPECT_EQ(constFirstWorld.tryGetComponent<VelocityComponent>(firstEntity.id()), nullptr);

    usize visits = 0u;
    firstView.each([&](NWB::Core::ECS::EntityID entityId, PoolSlotComponent<0u>& component){
        EXPECT_EQ(entityId, firstEntity.id());
        EXPECT_EQ(&component, &firstComponent);
        EXPECT_EQ(component.value, 17u);
        ++visits;
    });
    EXPECT_EQ(visits, 1u);

    firstEntity.addComponent<PoolSlotComponent<31u>>().value = 53u;
    EXPECT_EQ(firstEntity.getComponent<PoolSlotComponent<31u>>().value, 53u);
    EXPECT_EQ(secondEntity.getComponent<PoolSlotComponent<31u>>().value, 29u);
}

TEST(Ecs, ComponentPoolsCanBeRecreatedAfterWorldClear){
    TestWorld testWorld;
    auto entity = testWorld.world.createEntity();
    entity.addComponent<PoolSlotComponent<63u>>().value = 19u;
    entity.addComponent<PositionComponent>().x = 23;

    testWorld.world.clear();
    EXPECT_EQ(testWorld.world.entityCount(), 0u);
    EXPECT_EQ(testWorld.world.componentMutationVersion<PositionComponent>(), 0u);
    EXPECT_EQ(testWorld.world.componentMutationVersion<PoolSlotComponent<63u>>(), 0u);
    EXPECT_EQ(testWorld.world.tryGetComponent<PoolSlotComponent<63u>>(entity.id()), nullptr);
    EXPECT_EQ(testWorld.world.view<PositionComponent>().candidateCount(), 0u);

    auto recreated = testWorld.world.createEntity();
    recreated.addComponent<PoolSlotComponent<63u>>().value = 37u;
    EXPECT_EQ(recreated.getComponent<PoolSlotComponent<63u>>().value, 37u);
    EXPECT_EQ(testWorld.world.componentMutationVersion<PoolSlotComponent<63u>>(), 1u);
    EXPECT_FALSE(recreated.hasComponent<PositionComponent>());
    EXPECT_EQ(testWorld.world.view<PoolSlotComponent<63u>>().candidateCount(), 1u);
}

TEST(Ecs, RepeatedComponentLookupWorkload){
    TestWorld testWorld;
    static constexpr usize s_EntityCount = 4096u;
    static constexpr usize s_RoundCount = 64u;
    Array<NWB::Core::ECS::EntityID, s_EntityCount> entities;
    u64 expectedPositionSum = 0u;
    u64 expectedVelocitySum = 0u;
    for(usize i = 0u; i < s_EntityCount; ++i){
        auto entity = testWorld.world.createEntity();
        entities[i] = entity.id();
        entity.addComponent<PositionComponent>().x = static_cast<i32>(i + 1u);
        expectedPositionSum += i + 1u;
        if((i & 1u) == 0u){
            entity.addComponent<VelocityComponent>().x = static_cast<i32>(i + 3u);
            expectedVelocitySum += i + 3u;
        }
    }

    u64 positionSum = 0u;
    u64 velocitySum = 0u;
    usize absentComponents = 0u;
    const NWB::Core::ECS::World& constWorld = testWorld.world;
    const Timer lookupBegin = TimerNow();
    for(usize round = 0u; round < s_RoundCount; ++round){
        for(usize i = 0u; i < s_EntityCount; ++i){
            const auto entityId = entities[(i * 2053u + round * 17u) & (s_EntityCount - 1u)];
            if(auto* position = testWorld.world.tryGetComponent<PositionComponent>(entityId)){
                positionSum += static_cast<u64>(position->x);
                ++position->y;
            }
            if(const auto* velocity = constWorld.tryGetComponent<VelocityComponent>(entityId))
                velocitySum += static_cast<u64>(velocity->x);
            if(!constWorld.tryGetComponent<OverAlignedComponent>(entityId))
                ++absentComponents;
        }
    }
    const u64 lookupNanoseconds = DurationInNS<u64>(TimerNow(), lookupBegin);
    char durationText[32u] = {};
    RecordProperty("lookup_ns", FormatDecimal(lookupNanoseconds, durationText).data());

    EXPECT_EQ(positionSum, expectedPositionSum * s_RoundCount);
    EXPECT_EQ(velocitySum, expectedVelocitySum * s_RoundCount);
    EXPECT_EQ(absentComponents, s_EntityCount * s_RoundCount);
    testWorld.world.view<PositionComponent>().each([](NWB::Core::ECS::EntityID, PositionComponent& position){
        EXPECT_EQ(position.y, static_cast<i32>(s_RoundCount));
    });
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
    NWB::Core::Alloc::CpuTaskScheduler taskScheduler(3u);
    NWB::Core::ECS::World world(arena, taskScheduler);

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
        world.taskScope(),
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
        world.taskScope(),
        16u,
        [&pairVisits](NWB::Core::ECS::EntityID, PositionComponent& position, VelocityComponent& velocity){
            position.x += velocity.x;
            pairVisits.fetch_add(1u, MemoryOrder::relaxed);
        }
    );

    EXPECT_EQ(pairVisits.load(MemoryOrder::relaxed), s_EntityCount / 2u);
}

TEST(Ecs, ParallelEachNestedInTaskBatchCompletes){
    NWB::Core::Alloc::GlobalArena arena(s_EcsParallelTestArena);
    NWB::Core::Alloc::CpuTaskScheduler taskScheduler(3u);
    NWB::Core::ECS::World world(arena, taskScheduler);

    static constexpr u32 s_EntityCount = 64u;
    static constexpr usize s_OuterCount = 4u;
    for(u32 i = 0u; i < s_EntityCount; ++i){
        auto entity = world.createEntity();
        auto& position = entity.addComponent<PositionComponent>();
        position.x = static_cast<i32>(i);
    }

    Atomic<u32> visits{ 0u };
    world.taskScope().parallelFor(
        static_cast<usize>(0),
        s_OuterCount,
        [&](usize outerIndex){
            static_cast<void>(outerIndex);
            world.view<PositionComponent>().parallelEach(
                world.taskScope(),
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


TEST(Ecs, SystemDependenciesPreserveRegistrationOrderAcrossComponents){
    TestWorld testWorld;
    const auto positionType = NWB::Core::ECS::ComponentType<PositionComponent>();
    const auto velocityType = NWB::Core::ECS::ComponentType<VelocityComponent>();
    i32 position = 0;
    i32 velocity = 0;
    i32 observed = 0;

    ScheduledSystem writer(testWorld.arena, { { positionType, NWB::Core::ECS::AccessMode::Write } }, [&](){ position = 17; });
    ScheduledSystem transfer(testWorld.arena, {
        { positionType, NWB::Core::ECS::AccessMode::Read },
        { velocityType, NWB::Core::ECS::AccessMode::Write },
    }, [&](){ velocity = position + 1; });
    ScheduledSystem reader(testWorld.arena, { { velocityType, NWB::Core::ECS::AccessMode::Read } }, [&](){ observed = velocity; });

    NWB::Core::ECS::SystemScheduler scheduler(testWorld.arena);
    scheduler.addSystem(writer);
    scheduler.addSystem(transfer);
    scheduler.addSystem(reader);
    scheduler.execute(testWorld.world, 0.0f);
    EXPECT_EQ(observed, 18);

    // Removing and adding a system invalidates the cached predecessor list.
    scheduler.removeSystem(transfer);
    velocity = 31;
    scheduler.execute(testWorld.world, 0.0f);
    EXPECT_EQ(observed, 31);
    scheduler.addSystem(transfer);
    velocity = 47;
    scheduler.execute(testWorld.world, 0.0f);
    EXPECT_EQ(observed, 47);
    EXPECT_EQ(velocity, 18);
}

TEST(Ecs, SystemDependentsProceedWithoutWaitingForUnrelatedWork){
    NWB::Core::Alloc::GlobalArena arena(s_EcsParallelTestArena);
    NWB::Core::Alloc::CpuTaskScheduler taskScheduler(3u);
    NWB::Core::ECS::World world(arena, taskScheduler);
    const auto positionType = NWB::Core::ECS::ComponentType<PositionComponent>();
    Atomic<bool> dependentCompleted{ false };
    Atomic<bool> independentObservedCompletion{ false };
    i32 value = 0;

    ScheduledSystem writer(arena, { { positionType, NWB::Core::ECS::AccessMode::Write } }, [&](){ value = 5; });
    ScheduledSystem independent(arena, {}, [&](){
        const Timer begin = TimerNow();
        while(!dependentCompleted.load(MemoryOrder::acquire) && DurationInMS<u64>(TimerNow(), begin) < 2000u)
            SleepMS(1u);
        independentObservedCompletion.store(dependentCompleted.load(MemoryOrder::acquire), MemoryOrder::release);
    });
    ScheduledSystem dependent(arena, { { positionType, NWB::Core::ECS::AccessMode::Read } }, [&](){
        EXPECT_EQ(value, 5);
        dependentCompleted.store(true, MemoryOrder::release);
    });

    NWB::Core::ECS::SystemScheduler scheduler(arena);
    scheduler.addSystem(writer);
    scheduler.addSystem(independent);
    scheduler.addSystem(dependent);
    scheduler.execute(world, 0.0f);
    EXPECT_TRUE(independentObservedCompletion.load(MemoryOrder::acquire));
}

TEST(Ecs, MainThreadSystemRespectsWorkerDependencies){
    NWB::Core::Alloc::GlobalArena arena(s_EcsParallelTestArena);
    NWB::Core::Alloc::CpuTaskScheduler taskScheduler(2u);
    NWB::Core::ECS::World world(arena, taskScheduler);
    const auto positionType = NWB::Core::ECS::ComponentType<PositionComponent>();
    s_EcsCallerThread = true;
    bool executedOnCaller = false;
    i32 value = 0;

    ScheduledSystem worker(arena, { { positionType, NWB::Core::ECS::AccessMode::Write } }, [&](){ value = 23; });
    ScheduledSystem caller(arena, { { positionType, NWB::Core::ECS::AccessMode::Write } }, [&](){
        executedOnCaller = s_EcsCallerThread;
        EXPECT_EQ(value, 23);
        value = 29;
    }, { .target = NWB::Core::Alloc::CpuTaskTarget::MainThread });
    ScheduledSystem consumer(arena, { { positionType, NWB::Core::ECS::AccessMode::Read } }, [&](){ EXPECT_EQ(value, 29); });

    NWB::Core::ECS::SystemScheduler scheduler(arena);
    scheduler.addSystem(worker);
    scheduler.addSystem(caller);
    scheduler.addSystem(consumer);
    scheduler.execute(world, 0.0f);
    EXPECT_TRUE(executedOnCaller);
    s_EcsCallerThread = false;
}

TEST(Ecs, DependentSystemObservesAllNestedQueryTasks){
    NWB::Core::Alloc::GlobalArena arena(s_EcsParallelTestArena);
    NWB::Core::Alloc::CpuTaskScheduler taskScheduler(2u);
    NWB::Core::ECS::World world(arena, taskScheduler);
    static constexpr usize s_EntityCount = 512u;
    for(usize i = 0u; i < s_EntityCount; ++i)
        world.createEntity().addComponent<PositionComponent>().x = static_cast<i32>(i);
    const auto positionType = NWB::Core::ECS::ComponentType<PositionComponent>();
    usize observed = 0u;

    ScheduledSystem writer(arena, { { positionType, NWB::Core::ECS::AccessMode::Write } }, [&](){
        world.view<PositionComponent>().parallelEach(world.taskScope(), 8u, [](NWB::Core::ECS::EntityID, PositionComponent& position){
            position.y = position.x + 1;
        });
    });
    ScheduledSystem reader(arena, { { positionType, NWB::Core::ECS::AccessMode::Read } }, [&](){
        world.view<PositionComponent>().each([&](NWB::Core::ECS::EntityID, PositionComponent& position){
            EXPECT_EQ(position.y, position.x + 1);
            ++observed;
        });
    });

    NWB::Core::ECS::SystemScheduler scheduler(arena);
    scheduler.addSystem(writer);
    scheduler.addSystem(reader);
    scheduler.execute(world, 0.0f);
    EXPECT_EQ(observed, s_EntityCount);
}

TEST(Ecs, SystemCompletionIncludesAsynchronousDescendants){
    NWB::Core::Alloc::GlobalArena arena(s_EcsParallelTestArena);
    NWB::Core::Alloc::CpuTaskScheduler taskScheduler(2u);
    NWB::Core::ECS::World world(arena, taskScheduler);
    const auto positionType = NWB::Core::ECS::ComponentType<PositionComponent>();
    Atomic<i32> value{ 0 };
    i32 observed = 0;

    ScheduledSystem writer(arena, { { positionType, NWB::Core::ECS::AccessMode::Write } }, [&](){
        world.taskScope().submit([&](){
            SleepMS(25u);
            world.taskScope().submit([&](){ value.store(41, MemoryOrder::release); });
        });
    });
    ScheduledSystem reader(arena, { { positionType, NWB::Core::ECS::AccessMode::Read } }, [&](){
        observed = value.load(MemoryOrder::acquire);
    });

    NWB::Core::ECS::SystemScheduler scheduler(arena);
    scheduler.addSystem(writer);
    scheduler.addSystem(reader);
    scheduler.execute(world, 0.0f);
    EXPECT_EQ(observed, 41);
}


TEST(Ecs, WorldClearDoesNotWaitForUnrelatedSchedulerTasks){
    NWB::Core::Alloc::GlobalArena arena(s_EcsParallelTestArena);
    NWB::Core::Alloc::CpuTaskScheduler taskScheduler(2u);
    NWB::Core::Alloc::CpuTaskScope unrelatedTasks(taskScheduler);
    NWB::Core::ECS::World world(arena, taskScheduler);
    Atomic<bool> started{ false };
    Atomic<bool> release{ false };
    Atomic<bool> completed{ false };
    unrelatedTasks.submit([&](){
        started.store(true, MemoryOrder::release);
        started.notify_all();
        const Timer begin = TimerNow();
        while(!release.load(MemoryOrder::acquire) && DurationInMS<u64>(TimerNow(), begin) < 2000u)
            SleepMS(1u);
        completed.store(true, MemoryOrder::release);
    });
    started.wait(false, MemoryOrder::acquire);
    world.createEntity().addComponent<PositionComponent>();
    world.clear();
    const bool unrelatedStillRunning = !completed.load(MemoryOrder::acquire);
    release.store(true, MemoryOrder::release);
    unrelatedTasks.wait();
    EXPECT_TRUE(unrelatedStillRunning);
    EXPECT_EQ(world.entityCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

