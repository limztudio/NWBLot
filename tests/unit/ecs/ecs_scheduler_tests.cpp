// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/ecs/module.h>
#include <core/common/module.h>

#include <tests/common/ecs_test_world.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <global/atomic.h>
#include <global/compile.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_scheduler_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_SystemCompletionTimeoutMS = __hidden_ecs_tests::s_EcsTestSystemCompletionTimeoutMS;
constexpr u32 s_PollIntervalMS = __hidden_ecs_tests::s_EcsTestPollIntervalMS;
constexpr u32 s_EntityWaitIntervalMS = __hidden_ecs_tests::s_EcsTestEntityWaitIntervalMS;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestWorld = NWB::Tests::EcsTestWorld;

inline constexpr Name s_EcsParallelTestArena = __hidden_ecs_tests::s_EcsTestParallelArenaName;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using PositionComponent = __hidden_ecs_tests::PositionComponent;
using VelocityComponent = __hidden_ecs_tests::VelocityComponent;


class CountingSystem final : public NWB::Core::ECS::ISystem{
public:
    explicit CountingSystem(NWB::Core::Alloc::GlobalArena& arena)
        : NWB::Core::ECS::ISystem(arena)
    {
        writeAccess<PositionComponent>();
    }


public:
    virtual void prepare([[maybe_unused]] NWB::Core::ECS::World& world)noexcept override{
        ++prepares;
    }

    [[nodiscard]] virtual Expected<void, NWB::Core::CpuTaskAdmissionError::Enum> update(NWB::Core::ECS::World& world, const f32 delta)noexcept override{
        ++updates;
        lastDelta = delta;

        world.view<PositionComponent>().each([](NWB::Core::ECS::EntityID, PositionComponent& position)noexcept{
            ++position.x;
        });
        return {};
    }


public:
    u32 prepares = 0;
    u32 updates = 0;
    f32 lastDelta = 0.0f;
};


class ScheduledSystem final : public NWB::Core::ECS::ISystem{
public:
    ScheduledSystem(NWB::Core::Alloc::GlobalArena& arena, InitializerList<NWB::Core::ECS::ComponentAccess> accesses, Function<void()> update, NWB::Core::CpuTaskOptions options = {})
        : NWB::Core::ECS::ISystem(arena)
        , m_update(Move(update))
        , m_options(options)
    {
        for(const auto& access : accesses)
            registerAccess(access.typeId, access.mode);
    }


public:
    [[nodiscard]] virtual Expected<void, NWB::Core::CpuTaskAdmissionError::Enum> update([[maybe_unused]] NWB::Core::ECS::World& world, [[maybe_unused]] f32 delta)override{
        m_update();
        return {};
    }

    [[nodiscard]] virtual NWB::Core::CpuTaskOptions taskOptions()const noexcept override{ return m_options; }


private:
    Function<void()> m_update;
    NWB::Core::CpuTaskOptions m_options;
};


class RejectingRangeSystem final : public NWB::Core::ECS::ISystem{
public:
    explicit RejectingRangeSystem(NWB::Core::Alloc::GlobalArena& arena)
        : NWB::Core::ECS::ISystem(arena)
    {
        writeAccess<PositionComponent>();
    }


public:
    [[nodiscard]] virtual Expected<void, NWB::Core::CpuTaskAdmissionError::Enum> update(NWB::Core::ECS::World& world, [[maybe_unused]] f32 delta)override{
        NWB::Core::CpuTaskOptions options;
        options.target = static_cast<NWB::Core::CpuTaskTarget::Enum>(NWB::Core::CpuTaskTarget::MainThread + 1u);
        return world.taskScope().parallelFor(0u, 1u, 1u, [this](usize)noexcept{ ++callbacks; }, options);
    }


public:
    u32 callbacks = 0u;
};


static thread_local b8 s_EcsCallerThread = false;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Ecs, ParallelEachNestedInTaskBatchCompletes){
    NWB::Core::Alloc::GlobalArena arena(s_EcsParallelTestArena);
    NWB::Core::CpuTaskScheduler taskScheduler(3u);
    NWB::Core::ECS::World world(arena, taskScheduler);

    static constexpr u32 s_EntityCount = __hidden_ecs_tests::s_EcsTestNestedEntityCount;
    static constexpr usize s_OuterCount = __hidden_ecs_tests::s_EcsTestNestedOuterCount;
    for(u32 i = 0u; i < s_EntityCount; ++i){
        auto entity = world.createEntity();
        auto& position = entity.addComponent<PositionComponent>();
        position.x = static_cast<i32>(i);
    }

    Atomic<u32> visits{ 0u };
    EXPECT_TRUE((world.taskScope().parallelFor(static_cast<usize>(0), s_OuterCount, [&]([[maybe_unused]] usize outerIndex){
        EXPECT_TRUE((world.view<PositionComponent>().parallelEach(world.taskScope(), 1u, [&visits](NWB::Core::ECS::EntityID, [[maybe_unused]] PositionComponent& position)noexcept{
            visits.fetch_add(1u, MemoryOrder::relaxed);
        })));
    })));

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
    ASSERT_TRUE(scheduler.execute(testWorld.world, 0.5f));

    EXPECT_EQ(system.prepares, 1u);
    EXPECT_EQ(system.updates, 1u);
    EXPECT_EQ(position.x, 2);
}


TEST(Ecs, WorldTickPropagatesRangeRejectionAndSkipsDependentSystems){
    TestWorld testWorld;
    auto& rejectedSystem = testWorld.world.addSystem<RejectingRangeSystem>();
    auto& dependent = testWorld.world.addSystem<CountingSystem>();
    u32 independentUpdates = 0u;
    testWorld.world.addSystem<ScheduledSystem>(InitializerList<NWB::Core::ECS::ComponentAccess>{}, Function<void()>([&]()noexcept{ ++independentUpdates; }));
    const auto updated = testWorld.world.tick(0.5f);
    ASSERT_FALSE(updated);
    EXPECT_EQ(updated.error(), NWB::Core::CpuTaskAdmissionError::InvalidOptions);
    EXPECT_EQ(rejectedSystem.callbacks, 0u);
    EXPECT_EQ(dependent.prepares, 1u);
    EXPECT_EQ(dependent.updates, 0u);
    EXPECT_EQ(independentUpdates, 1u);
    EXPECT_EQ(testWorld.taskScheduler.statistics().outstandingTasks, 0u);
    u32 laterCallbacks = 0u;
    const auto admitted = testWorld.taskScheduler.submit([&]()noexcept{ ++laterCallbacks; });
    ASSERT_TRUE(admitted);
    testWorld.taskScheduler.wait(*admitted);
    EXPECT_EQ(laterCallbacks, 1u);
}


TEST(Ecs, SystemDependenciesPreserveRegistrationOrderAcrossComponents){
    TestWorld testWorld;
    const auto positionType = NWB::Core::ECS::ComponentType<PositionComponent>();
    const auto velocityType = NWB::Core::ECS::ComponentType<VelocityComponent>();
    i32 position = 0;
    i32 velocity = 0;
    i32 observed = 0;

    ScheduledSystem writer(testWorld.arena, { { positionType, NWB::Core::ECS::AccessMode::Write } }, [&]()noexcept{ position = 17; });
    ScheduledSystem transfer(testWorld.arena, {
        { positionType, NWB::Core::ECS::AccessMode::Read },
        { velocityType, NWB::Core::ECS::AccessMode::Write },
    }, [&]()noexcept{ velocity = position + 1; });
    ScheduledSystem reader(testWorld.arena, { { velocityType, NWB::Core::ECS::AccessMode::Read } }, [&]()noexcept{ observed = velocity; });

    NWB::Core::ECS::SystemScheduler scheduler(testWorld.arena);
    scheduler.addSystem(writer);
    scheduler.addSystem(transfer);
    scheduler.addSystem(reader);
    ASSERT_TRUE(scheduler.execute(testWorld.world, 0.0f));
    EXPECT_EQ(observed, 18);

    // Removing and adding a system invalidates the cached predecessor list.
    scheduler.removeSystem(transfer);
    velocity = 31;
    ASSERT_TRUE(scheduler.execute(testWorld.world, 0.0f));
    EXPECT_EQ(observed, 31);
    scheduler.addSystem(transfer);
    velocity = 47;
    ASSERT_TRUE(scheduler.execute(testWorld.world, 0.0f));
    EXPECT_EQ(observed, 47);
    EXPECT_EQ(velocity, 18);
}

TEST(Ecs, SystemDependentsProceedWithoutWaitingForUnrelatedWork){
    NWB::Core::Alloc::GlobalArena arena(s_EcsParallelTestArena);
    NWB::Core::CpuTaskScheduler taskScheduler(3u);
    NWB::Core::ECS::World world(arena, taskScheduler);
    const auto positionType = NWB::Core::ECS::ComponentType<PositionComponent>();
    Atomic<b8> dependentCompleted{ false };
    Atomic<b8> independentObservedCompletion{ false };
    i32 value = 0;

    ScheduledSystem writer(arena, { { positionType, NWB::Core::ECS::AccessMode::Write } }, [&]()noexcept{ value = 5; });
    ScheduledSystem independent(arena, {}, [&]()noexcept{
        const Timer begin = TimerNow();
        while(!dependentCompleted.load(MemoryOrder::acquire) && DurationInMS<u64>(TimerNow(), begin) < __hidden_ecs_scheduler_tests::s_SystemCompletionTimeoutMS)
            SleepMS(__hidden_ecs_scheduler_tests::s_PollIntervalMS);
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
    ASSERT_TRUE(scheduler.execute(world, 0.0f));
    EXPECT_TRUE(independentObservedCompletion.load(MemoryOrder::acquire));
}

TEST(Ecs, MainThreadSystemRespectsWorkerDependencies){
    NWB::Core::Alloc::GlobalArena arena(s_EcsParallelTestArena);
    NWB::Core::CpuTaskScheduler taskScheduler(2u);
    NWB::Core::ECS::World world(arena, taskScheduler);
    const auto positionType = NWB::Core::ECS::ComponentType<PositionComponent>();
    s_EcsCallerThread = true;
    b8 executedOnCaller = false;
    i32 value = 0;

    ScheduledSystem worker(arena, { { positionType, NWB::Core::ECS::AccessMode::Write } }, [&]()noexcept{ value = 23; });
    ScheduledSystem caller(arena, { { positionType, NWB::Core::ECS::AccessMode::Write } }, [&](){
        executedOnCaller = s_EcsCallerThread;
        EXPECT_EQ(value, 23);
        value = 29;
    }, { .target = NWB::Core::CpuTaskTarget::MainThread });
    ScheduledSystem consumer(arena, { { positionType, NWB::Core::ECS::AccessMode::Read } }, [&](){ EXPECT_EQ(value, 29); });

    NWB::Core::ECS::SystemScheduler scheduler(arena);
    scheduler.addSystem(worker);
    scheduler.addSystem(caller);
    scheduler.addSystem(consumer);
    ASSERT_TRUE(scheduler.execute(world, 0.0f));
    EXPECT_TRUE(executedOnCaller);
    s_EcsCallerThread = false;
}

TEST(Ecs, DependentSystemObservesAllNestedQueryTasks){
    NWB::Core::Alloc::GlobalArena arena(s_EcsParallelTestArena);
    NWB::Core::CpuTaskScheduler taskScheduler(2u);
    NWB::Core::ECS::World world(arena, taskScheduler);
    static constexpr usize s_EntityCount = __hidden_ecs_tests::s_EcsTestDependentQueryEntityCount;
    for(usize i = 0u; i < s_EntityCount; ++i)
        world.createEntity().addComponent<PositionComponent>().x = static_cast<i32>(i);
    const auto positionType = NWB::Core::ECS::ComponentType<PositionComponent>();
    usize observed = 0u;

    ScheduledSystem writer(arena, { { positionType, NWB::Core::ECS::AccessMode::Write } }, [&](){
        EXPECT_TRUE((world.view<PositionComponent>().parallelEach(world.taskScope(), 8u, [](NWB::Core::ECS::EntityID, PositionComponent& position)noexcept{
            position.y = position.x + 1;
        })));
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
    ASSERT_TRUE(scheduler.execute(world, 0.0f));
    EXPECT_EQ(observed, s_EntityCount);
}

TEST(Ecs, SystemCompletionIncludesAsynchronousDescendants){
    NWB::Core::Alloc::GlobalArena arena(s_EcsParallelTestArena);
    NWB::Core::CpuTaskScheduler taskScheduler(2u);
    NWB::Core::ECS::World world(arena, taskScheduler);
    const auto positionType = NWB::Core::ECS::ComponentType<PositionComponent>();
    Atomic<i32> value{ 0 };
    i32 observed = 0;

    ScheduledSystem writer(arena, { { positionType, NWB::Core::ECS::AccessMode::Write } }, [&](){
        const auto cpuTaskAdmission = world.taskScope().submit([&](){
            SleepMS(s_EntityWaitIntervalMS);
            const auto secondCpuTaskAdmission = world.taskScope().submit([&]()noexcept{ value.store(41, MemoryOrder::release); });
            ASSERT_TRUE(secondCpuTaskAdmission);
            EXPECT_TRUE(secondCpuTaskAdmission->valid());
        });
        ASSERT_TRUE(cpuTaskAdmission);
        EXPECT_TRUE(cpuTaskAdmission->valid());
    });
    ScheduledSystem reader(arena, { { positionType, NWB::Core::ECS::AccessMode::Read } }, [&]()noexcept{
        observed = value.load(MemoryOrder::acquire);
    });

    NWB::Core::ECS::SystemScheduler scheduler(arena);
    scheduler.addSystem(writer);
    scheduler.addSystem(reader);
    ASSERT_TRUE(scheduler.execute(world, 0.0f));
    EXPECT_EQ(observed, 41);
}


TEST(Ecs, WorldClearDoesNotWaitForUnrelatedSchedulerTasks){
    NWB::Core::Alloc::GlobalArena arena(s_EcsParallelTestArena);
    NWB::Core::CpuTaskScheduler taskScheduler(2u);
    NWB::Core::CpuTaskScope unrelatedTasks(taskScheduler);
    NWB::Core::ECS::World world(arena, taskScheduler);
    Atomic<b8> started{ false };
    Atomic<b8> release{ false };
    Atomic<b8> completed{ false };
    const auto cpuTaskAdmission = unrelatedTasks.submit([&]()noexcept{
        started.store(true, MemoryOrder::release);
        started.notify_all();
        const Timer begin = TimerNow();
        while(!release.load(MemoryOrder::acquire) && DurationInMS<u64>(TimerNow(), begin) < __hidden_ecs_scheduler_tests::s_SystemCompletionTimeoutMS)
            SleepMS(__hidden_ecs_scheduler_tests::s_PollIntervalMS);
        completed.store(true, MemoryOrder::release);
    });
    ASSERT_TRUE(cpuTaskAdmission);
    EXPECT_TRUE(cpuTaskAdmission->valid());
    started.wait(false, MemoryOrder::acquire);
    world.createEntity().addComponent<PositionComponent>();
    world.clear();
    const b8 unrelatedStillRunning = !completed.load(MemoryOrder::acquire);
    release.store(true, MemoryOrder::release);
    unrelatedTasks.wait();
    EXPECT_TRUE(unrelatedStillRunning);
    EXPECT_EQ(world.entityCount(), 0u);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

