// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpu_task_scheduler_test_support.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_scheduler_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;

using namespace CpuTaskSchedulerTestSupport;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskSchedulerTests, DependencyFanInWaitsForEveryPredecessorIncludingDuplicateEdges){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    TaskGate rootGate;
    CpuTaskScheduler scheduler(HomogeneousWorkers(s_ExpectedDualCount));
    ScopeExit releaseRoot([&]()noexcept{ rootGate.open(); });
    Atomic<u32> completedBits{ 0u };
    Atomic<u32> observedBits{ 0u };
    const auto rootTaskAdmission = scheduler.submit([&]()noexcept{ rootGate.block(); });
    ASSERT_TRUE(rootTaskAdmission);
    const auto root = *rootTaskAdmission;
    EXPECT_TRUE(CpuTaskTestSupport::WaitUntil(rootGate.entered, s_GateTimeoutMS));
    CpuTaskHandle dependencies[6u];
    for(u32 index = 0u; index < 4u; ++index){
        const auto cpuTaskAdmission = scheduler.submit([&completedBits, index]()noexcept{
            completedBits.fetch_or(1u << index, MemoryOrder::release);
        }, root
        );
        ASSERT_TRUE(cpuTaskAdmission);
        dependencies[index] = *cpuTaskAdmission;
        EXPECT_TRUE(dependencies[index].valid());
    }
    dependencies[4u] = dependencies[1u];
    dependencies[5u] = {};
    const auto secondCpuTaskAdmission = scheduler.submit([&]()noexcept{
        observedBits.store(completedBits.load(MemoryOrder::acquire), MemoryOrder::release);
    }, {}, dependencies, 6u
    );
    ASSERT_TRUE(secondCpuTaskAdmission);
    const auto join = *secondCpuTaskAdmission;
    EXPECT_TRUE(join.valid());
    EXPECT_FALSE(scheduler.isComplete(join));
    rootGate.open();
    scheduler.wait(join);
    EXPECT_EQ(observedBits.load(), 15u);
    EXPECT_FALSE(rootGate.expired.load());
    EXPECT_EQ(scheduler.statistics().completedTasks, 6u);
}


TEST(CpuTaskSchedulerTests, ForeignDependenciesAndForeignWaitsAreRejectedWithoutPublishingWork){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler first(0u);
    CpuTaskScheduler second(0u);
    const auto foreignTaskAdmission = first.submit([]()noexcept{});
    ASSERT_TRUE(foreignTaskAdmission);
    const auto foreign = *foreignTaskAdmission;
    u32 invoked = 0u;
    const auto rejected = second.submit([&]()noexcept{ ++invoked; }, foreign);
    EXPECT_FALSE(rejected);
    EXPECT_EQ(second.statistics().outstandingTasks, 0u);
    EXPECT_THROW(second.wait(foreign), RuntimeException);
    first.wait(foreign);
    const auto cpuTaskAdmission = second.submit([&]()noexcept{ ++invoked; }, foreign);
    EXPECT_FALSE(cpuTaskAdmission);
    EXPECT_EQ(invoked, 0u);
}


TEST(CpuTaskSchedulerTests, RetiredHandlesStayCompleteWhenNodeStorageIsReused){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler scheduler(0u);
    const auto retiredTaskAdmission = scheduler.submit([]()noexcept{});
    ASSERT_TRUE(retiredTaskAdmission);
    const auto retired = *retiredTaskAdmission;
    scheduler.wait(retired);
    u32 invoked = 0u;
    const auto pendingTaskAdmission = scheduler.submit([&]()noexcept{ ++invoked; });
    ASSERT_TRUE(pendingTaskAdmission);
    const auto pending = *pendingTaskAdmission;
    EXPECT_TRUE(scheduler.isComplete(retired));
    EXPECT_FALSE(scheduler.isComplete(pending));
    scheduler.wait(retired);
    EXPECT_EQ(invoked, 0u);
    scheduler.wait(pending);
    EXPECT_EQ(invoked, 1u);
}


TEST(CpuTaskSchedulerTests, ParentCompletionRetainsAncestorCapturesUntilGrandchildrenAndRetirementFinish){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    TaskGate grandchildGate;
    Atomic<u32> retirements{ 0u };
    Atomic<u32> total{ 0u };
    Atomic<u32> retirementsSeenByDependent{ 0u };
    Atomic<bool> rejected{ false };
    CpuTaskScheduler scheduler(HomogeneousWorkers(s_ExpectedDualCount));
    ScopeExit releaseGrandchild([&]()noexcept{ grandchildGate.open(); });
    const auto cpuTaskAdmission = scheduler.submit([&scheduler, &grandchildGate, &retirements, &total, &rejected, parentCapture = LifetimeProbe(retirements, 11u)]() mutable{
        const auto secondCpuTaskAdmission = scheduler.submit([&scheduler, &grandchildGate, &retirements, &total, &rejected, &parentCapture, childCapture = LifetimeProbe(retirements, 13u)]() mutable{
            const auto thirdCpuTaskAdmission = scheduler.submit([&grandchildGate, &total, &parentCapture, &childCapture, grandchildCapture = LifetimeProbe(retirements, 17u)]()noexcept{
                grandchildGate.block();
                total.store(parentCapture.value + childCapture.value + grandchildCapture.value, MemoryOrder::release);
            }
            );
            ASSERT_TRUE(thirdCpuTaskAdmission);
            const auto grandchild = *thirdCpuTaskAdmission;
            if(!grandchild.valid())
                rejected.store(true, MemoryOrder::release);
        }
        );
        ASSERT_TRUE(secondCpuTaskAdmission);
        const auto child = *secondCpuTaskAdmission;
        if(!child.valid())
            rejected.store(true, MemoryOrder::release); }
    );
    ASSERT_TRUE(cpuTaskAdmission);
    const auto parent = *cpuTaskAdmission;
    const auto fourthCpuTaskAdmission = scheduler.submit([&]()noexcept{
        retirementsSeenByDependent.store(retirements.load(MemoryOrder::acquire), MemoryOrder::release);
    }, parent
    );
    ASSERT_TRUE(fourthCpuTaskAdmission);
    const auto dependent = *fourthCpuTaskAdmission;
    EXPECT_TRUE(CpuTaskTestSupport::WaitUntil(grandchildGate.entered, s_GateTimeoutMS));
    EXPECT_FALSE(scheduler.isComplete(parent));
    EXPECT_FALSE(scheduler.isComplete(dependent));
    EXPECT_EQ(retirements.load(), 0u);
    grandchildGate.open();
    scheduler.wait(dependent);
    EXPECT_FALSE(rejected.load());
    EXPECT_FALSE(grandchildGate.expired.load());
    EXPECT_EQ(total.load(), 41u);
    EXPECT_EQ(retirements.load(), 3u);
    EXPECT_EQ(retirementsSeenByDependent.load(), 3u);
}


TEST(CpuTaskSchedulerTests, ScopeJoinDoesNotWaitForAnUnrelatedRunningScope){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    TaskGate unrelatedGate;
    CpuTaskScheduler scheduler(HomogeneousWorkers(s_ExpectedDualCount));
    CpuTaskScope unrelated(scheduler);
    CpuTaskScope owned(scheduler);
    ScopeExit releaseUnrelated([&]()noexcept{ unrelatedGate.open(); });
    const auto unrelatedTaskAdmission = unrelated.submit([&]()noexcept{ unrelatedGate.block(); });
    ASSERT_TRUE(unrelatedTaskAdmission);
    const auto unrelatedTask = *unrelatedTaskAdmission;
    EXPECT_TRUE(CpuTaskTestSupport::WaitUntil(unrelatedGate.entered, s_GateTimeoutMS));
    Atomic<bool> ownedFinished{ false };
    const auto cpuTaskAdmission = owned.submit([&]()noexcept{ ownedFinished.store(true, MemoryOrder::release); });
    EXPECT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        EXPECT_TRUE(cpuTaskAdmission->valid());
    owned.wait();
    EXPECT_TRUE(ownedFinished.load());
    EXPECT_FALSE(scheduler.isComplete(unrelatedTask));
    EXPECT_FALSE(unrelatedGate.expired.load());
    unrelatedGate.open();
    unrelated.wait();
}


TEST(CpuTaskSchedulerTests, ScopeDestructionIncludesChildrenSubmittedDirectlyToTheScheduler){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    Atomic<u32> retired{ 0u };
    Atomic<u32> invoked{ 0u };
    Atomic<bool> rejected{ false };
    CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
    {
        CpuTaskScope scope(scheduler);
        const auto cpuTaskAdmission = scope.submit([&](){
            const auto secondCpuTaskAdmission = scheduler.submit([&invoked, capture = LifetimeProbe(retired, 23u)]()noexcept{
                invoked.store(capture.value, MemoryOrder::release);
            }
            );
            if(!secondCpuTaskAdmission || !secondCpuTaskAdmission->valid())
                rejected.store(true, MemoryOrder::release);
        }
        );
        EXPECT_TRUE(cpuTaskAdmission);
        if(cpuTaskAdmission)
            EXPECT_TRUE(cpuTaskAdmission->valid());
    }
    EXPECT_FALSE(rejected.load());
    EXPECT_EQ(invoked.load(), 23u);
    EXPECT_EQ(retired.load(), 1u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskSchedulerTests, OneWorkerCompletesNestedParallelRangesThroughCooperativeExecution){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
    Atomic<u32> visited[35u]{};
    const auto cpuTaskAdmission = scheduler.submit([&](){
        EXPECT_TRUE((scheduler.parallelFor(0u, 5u, 1u, [&](const usize outer){
            EXPECT_TRUE((scheduler.parallelFor(0u, 7u, 1u, [&](const usize inner)noexcept{
                visited[outer * 7u + inner].fetch_add(1u, MemoryOrder::relaxed);
            }
            )));
        }
        )));
    }
    );
    ASSERT_TRUE(cpuTaskAdmission);
    const auto root = *cpuTaskAdmission;
    scheduler.wait(root);
    for(const auto& count : visited)
        EXPECT_EQ(count.load(), 1u);
    EXPECT_GT(scheduler.statistics().cooperativeTasks, 0u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskSchedulerTests, MainThreadTasksSubmittedByWorkersExecuteOnlyOnTheSchedulerOwner){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
    const ThreadId owner = QueryCurrentThreadId();
    Atomic<bool> published{ false };
    Atomic<bool> rejected{ false };
    Atomic<u32> invocations{ 0u };
    ThreadId actual;
    usize workerIndex = Limit<usize>::s_Max;
    const auto cpuTaskAdmission = scheduler.submit([&](){
        CpuTaskOptions mainOptions;
        mainOptions.target = CpuTaskTarget::MainThread;
        const auto secondCpuTaskAdmission = scheduler.submit([&]()noexcept{
            actual = QueryCurrentThreadId();
            workerIndex = scheduler.currentWorkerIndex();
            invocations.fetch_add(1u, MemoryOrder::release);
        }, mainOptions
        );
        if(!secondCpuTaskAdmission || !secondCpuTaskAdmission->valid())
            rejected.store(true, MemoryOrder::release);
        published.store(true, MemoryOrder::release);
    }
    );
    ASSERT_TRUE(cpuTaskAdmission);
    const auto parent = *cpuTaskAdmission;
    EXPECT_TRUE(CpuTaskTestSupport::WaitUntil(published, s_GateTimeoutMS));
    EXPECT_EQ(invocations.load(), 0u);
    scheduler.wait(parent);
    EXPECT_FALSE(rejected.load());
    EXPECT_EQ(actual, owner);
    EXPECT_EQ(workerIndex, 0u);
    EXPECT_EQ(invocations.load(), 1u);
}


TEST(CpuTaskSchedulerTests, ZeroWorkersMakeProgressAcrossChildrenDependenciesAndMainThreadTasks){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler scheduler(0u);
    const ThreadId owner = QueryCurrentThreadId();
    u32 value = 0u;
    bool rejected = false;
    ThreadId actual;
    const auto cpuTaskAdmission = scheduler.submit([&](){
        const auto childTaskAdmission = scheduler.submit([&]()noexcept{ value = 17u; });
        ASSERT_TRUE(childTaskAdmission);
        const auto child = *childTaskAdmission;
        CpuTaskOptions mainOptions;
        mainOptions.target = CpuTaskTarget::MainThread;
        const auto secondCpuTaskAdmission = scheduler.submit([&]()noexcept{ value *= 3u; actual = QueryCurrentThreadId(); }, mainOptions, &child, 1u);
        rejected = !secondCpuTaskAdmission || !secondCpuTaskAdmission->valid();
    }
    );
    ASSERT_TRUE(cpuTaskAdmission);
    const auto parent = *cpuTaskAdmission;
    scheduler.wait(parent);
    EXPECT_FALSE(rejected);
    EXPECT_EQ(value, 51u);
    EXPECT_EQ(actual, owner);
    EXPECT_EQ(scheduler.workerThreadCount(), 0u);
    EXPECT_EQ(scheduler.statistics().completedTasks, 3u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

