// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/cpu/scheduler.h>

#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskProfilingTests, DisabledCaptureProducesNoEvents){
    using namespace NWB::Core;
    CpuTaskScheduler scheduler(0u);
    scheduler.parallelFor(0u, 16u, [](usize){});
    scheduler.wait();
    CpuTaskProfileEvent events[8u];
    EXPECT_EQ(scheduler.readProfileEvents(events, 8u), 0u);
    const auto statistics = scheduler.statistics();
    EXPECT_FALSE(statistics.profileEnabled);
    EXPECT_EQ(statistics.profileRecordedEvents, 0u);
    EXPECT_EQ(statistics.profileDroppedEvents, 0u);
    EXPECT_EQ(statistics.profilePendingEvents, 0u);
}

TEST(CpuTaskProfilingTests, ReadyAndExecutionEventsRetainTheirOwnFramesAndLabels){
    using namespace NWB::Core;
    CpuTaskScheduler scheduler(0u);
    const Name scopeName("tests.cpu.scope");
    const Name taskName("tests.cpu.task");
    const auto scopeLabel = scheduler.registerProfileLabel(scopeName);
    const auto taskLabel = scheduler.registerProfileLabel(taskName);
    EXPECT_EQ(scheduler.registerProfileLabel(taskName).value, taskLabel.value);
    EXPECT_FALSE(scheduler.registerProfileLabel(NAME_NONE).valid());
    CpuTaskScope scope(scheduler, scopeLabel);
    scheduler.setProfiling(true, 7u);
    const auto task = scope.submit([](){ SleepMS(1u); }, { .profileLabel = taskLabel });
    ASSERT_TRUE(task.valid());
    ASSERT_TRUE(scope.submit([](){ SleepMS(1u); }).valid());
    SleepMS(1u);
    scheduler.setProfiling(true, 8u);
    scheduler.wait(task);
    scope.wait();
    scheduler.wait();
    CpuTaskProfileEvent events[16u];
    const usize count = scheduler.readProfileEvents(events, 16u);
    u32 queued = 0u;
    u32 executed = 0u;
    u32 handleJoins = 0u;
    u32 scopeJoins = 0u;
    u32 schedulerJoins = 0u;
    u64 epoch = 0u;
    for(usize index = 0u; index < count; ++index){
        const auto& event = events[index];
        if(epoch == 0u)
            epoch = event.captureEpoch;
        EXPECT_EQ(event.captureEpoch, epoch);
        EXPECT_EQ(event.workerIndex, 0u);
        EXPECT_GT(event.durationNanoseconds, 0u);
        if(event.kind == CpuTaskProfileKind::QueueDelay){
            ++queued;
            EXPECT_EQ(event.frameIndex, 7u);
            EXPECT_TRUE(event.label == scopeName || event.label == taskName);
        }
        else{
            EXPECT_EQ(event.frameIndex, 8u);
            if(event.kind == CpuTaskProfileKind::Execution){
                ++executed;
                EXPECT_TRUE(event.label == scopeName || event.label == taskName);
            }
            else if(event.kind == CpuTaskProfileKind::HandleJoin){
                ++handleJoins;
                EXPECT_EQ(event.label, taskName);
            }
            else if(event.kind == CpuTaskProfileKind::ScopeJoin){
                ++scopeJoins;
                EXPECT_EQ(event.label, scopeName);
            }
            else if(event.kind == CpuTaskProfileKind::SchedulerJoin)
                ++schedulerJoins;
        }
    }
    EXPECT_EQ(queued, 2u);
    EXPECT_EQ(executed, 2u);
    EXPECT_EQ(handleJoins, 1u);
    EXPECT_EQ(scopeJoins, 1u);
    EXPECT_EQ(schedulerJoins, 1u);
    EXPECT_EQ(scheduler.statistics().profileDroppedEvents, 0u);
}

TEST(CpuTaskProfilingTests, RingBufferPreservesOrderAcrossPartialDrainsAndReportsOverflow){
    using namespace NWB::Core;
    CpuTaskSchedulerConfig config;
    config.workerCount = 0u;
    config.profileEventCapacity = 4u;
    CpuTaskScheduler scheduler(config);
    scheduler.setProfiling(true);
    const auto first = scheduler.submit([](){});
    ASSERT_TRUE(first.valid());
    scheduler.wait(first);
    CpuTaskProfileEvent events[8u];
    ASSERT_EQ(scheduler.readProfileEvents(events, 2u), 2u);
    EXPECT_EQ(events[0u].kind, CpuTaskProfileKind::QueueDelay);
    EXPECT_EQ(events[1u].kind, CpuTaskProfileKind::Execution);
    const auto second = scheduler.submit([](){});
    ASSERT_TRUE(second.valid());
    scheduler.wait(second);
    ASSERT_EQ(scheduler.readProfileEvents(events, 8u), 4u);
    EXPECT_EQ(events[0u].kind, CpuTaskProfileKind::HandleJoin);
    EXPECT_EQ(events[0u].task.generation, first.generation);
    EXPECT_EQ(events[1u].kind, CpuTaskProfileKind::QueueDelay);
    EXPECT_EQ(events[2u].kind, CpuTaskProfileKind::Execution);
    EXPECT_EQ(events[3u].kind, CpuTaskProfileKind::HandleJoin);
    EXPECT_EQ(events[3u].task.generation, second.generation);
    const auto third = scheduler.submit([](){});
    ASSERT_TRUE(third.valid());
    scheduler.wait(third);
    scheduler.wait();
    scheduler.wait();
    EXPECT_EQ(scheduler.statistics().profilePendingEvents, 4u);
    EXPECT_EQ(scheduler.statistics().profileDroppedEvents, 1u);
    EXPECT_EQ(scheduler.readProfileEvents(nullptr, 4u), 0u);
    EXPECT_EQ(scheduler.readProfileEvents(events, 0u), 0u);
    EXPECT_EQ(scheduler.readProfileEvents(events, 8u), 4u);
    EXPECT_EQ(scheduler.readProfileEvents(events, 8u), 0u);
}

TEST(CpuTaskProfilingTests, CaptureRestartRejectsOldReadyAndExecutingTimers){
    using namespace NWB::Core;
    CpuTaskHandle child;
    CpuTaskScheduler scheduler(0u);
    scheduler.setProfiling(true, 1u);
    const auto parent = scheduler.submit([&](){
        scheduler.setProfiling(false);
        scheduler.setProfiling(true, 2u);
        child = scheduler.submit([](){});
        EXPECT_TRUE(child.valid());
    });
    ASSERT_TRUE(parent.valid());
    const auto oldReady = scheduler.submit([](){});
    ASSERT_TRUE(oldReady.valid());
    scheduler.wait(parent);
    CpuTaskProfileEvent events[16u];
    const usize count = scheduler.readProfileEvents(events, 16u);
    u32 executed = 0u;
    u32 queued = 0u;
    for(usize index = 0u; index < count; ++index){
        const auto& event = events[index];
        EXPECT_EQ(event.frameIndex, 2u);
        EXPECT_GT(event.captureEpoch, 1u);
        EXPECT_NE(event.task.index, parent.index);
        if(event.kind == CpuTaskProfileKind::Execution)
            ++executed;
        if(event.kind == CpuTaskProfileKind::QueueDelay){
            ++queued;
            EXPECT_EQ(event.task.index, child.index);
        }
    }
    EXPECT_EQ(executed, 2u);
    EXPECT_EQ(queued, 1u);
    scheduler.setProfiling(false);
    EXPECT_EQ(scheduler.readProfileEvents(events, 16u), 0u);
    EXPECT_FALSE(scheduler.statistics().profileEnabled);
}

TEST(CpuTaskProfilingTests, WorkerIdleEventsUseTheNativeWorkerLane){
    using namespace NWB::Core;
    CpuTaskSchedulerConfig config;
    config.workerCount = 1u;
    config.heterogeneous = false;
    CpuTaskScheduler scheduler(config);
    scheduler.setProfiling(true, 9u);
    bool idleObserved = false;
    const Timer deadline = TimerNow();
    while(!idleObserved && DurationInMS<u64>(TimerNow(), deadline) < 4000u){
        SleepMS(2u);
        const auto task = scheduler.submit([](){});
        ASSERT_TRUE(task.valid());
        scheduler.wait(task);
        CpuTaskProfileEvent events[32u];
        const usize count = scheduler.readProfileEvents(events, 32u);
        for(usize index = 0u; index < count; ++index){
            if(events[index].kind == CpuTaskProfileKind::WorkerIdle){
                idleObserved = true;
                EXPECT_EQ(events[index].workerIndex, 1u);
                EXPECT_EQ(events[index].affinity, CpuAffinity::Any);
                EXPECT_EQ(events[index].frameIndex, 9u);
                EXPECT_GT(events[index].durationNanoseconds, 0u);
            }
        }
    }
    EXPECT_TRUE(idleObserved);
}

TEST(CpuTaskProfilingTests, ForeignLabelsDoNotAliasLocalNames){
    using namespace NWB::Core;
    CpuTaskScheduler first(0u);
    CpuTaskScheduler second(0u);
    const Name name("tests.cpu.local_name");
    const auto foreign = first.registerProfileLabel(name);
    const auto local = second.registerProfileLabel(name);
    EXPECT_NE(foreign.value, local.value);
    second.setProfiling(true);
    const auto task = second.submit([](){}, { .profileLabel = foreign });
    ASSERT_TRUE(task.valid());
    second.wait(task);
    CpuTaskProfileEvent events[8u];
    const usize count = second.readProfileEvents(events, 8u);
    ASSERT_GT(count, 0u);
    for(usize index = 0u; index < count; ++index)
        EXPECT_EQ(events[index].label, NAME_NONE);
}

TEST(CpuTaskProfilingTests, ZeroCapacityLeavesCaptureDisabled){
    using namespace NWB::Core;
    CpuTaskSchedulerConfig config;
    config.workerCount = 0u;
    config.profileEventCapacity = 0u;
    CpuTaskScheduler scheduler(config);
    scheduler.setProfiling(true);
    scheduler.parallelFor(0u, 1u, [](usize){});
    EXPECT_FALSE(scheduler.statistics().profileEnabled);
    EXPECT_EQ(scheduler.statistics().profilePendingEvents, 0u);
    EXPECT_EQ(scheduler.statistics().profileDroppedEvents, 0u);
}


TEST(CpuTaskProfilingTests, ConcurrentCaptureChangesDoNotAffectTaskCompletionOrMixBufferedEpochs){
    using namespace NWB::Core;
    Atomic<bool> started{ false };
    Atomic<u32> callbacks{ 0u };
    CpuTaskSchedulerConfig config;
    config.workerCount = 2u;
    config.heterogeneous = false;
    config.profileEventCapacity = 16u;
    CpuTaskScheduler scheduler(config);
    CpuTaskScope scope(scheduler);
    Thread capture([&](){
        started.store(true, MemoryOrder::release);
        for(u64 frame = 1u; frame <= 64u; ++frame){
            scheduler.setProfiling(true, frame);
            SleepMS(1u);
            scheduler.setProfiling(false);
        }
    });
    while(!started.load(MemoryOrder::acquire))
        SleepMS(1u);
    for(u32 index = 0u; index < 256u; ++index){
        EXPECT_TRUE(scope.submit([&](){
            SleepMS(1u);
            callbacks.fetch_add(1u, MemoryOrder::release);
        }).valid());
    }
    while(callbacks.load(MemoryOrder::acquire) != 256u){
        CpuTaskProfileEvent events[8u];
        const usize count = scheduler.readProfileEvents(events, 8u);
        for(usize index = 0u; index < count; ++index){
            EXPECT_EQ(events[index].captureEpoch, events[0u].captureEpoch);
            EXPECT_GT(events[index].captureEpoch, 0u);
            if(events[index].task.valid())
                EXPECT_EQ(events[index].task.domainIdentity, scheduler.domainIdentity());
        }
        SleepMS(1u);
    }
    capture.join();
    scope.wait();
    EXPECT_EQ(scheduler.statistics().completedTasks, 256u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
    scheduler.setProfiling(true, 1000u);
    const auto probe = scheduler.submit([](){});
    ASSERT_TRUE(probe.valid());
    scheduler.wait(probe);
    CpuTaskProfileEvent events[16u];
    const usize count = scheduler.readProfileEvents(events, 16u);
    EXPECT_GT(count, 0u);
    for(usize index = 0u; index < count; ++index)
        EXPECT_EQ(events[index].frameIndex, 1000u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

