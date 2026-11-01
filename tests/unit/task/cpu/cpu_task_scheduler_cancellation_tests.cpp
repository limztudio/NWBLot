// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpu_task_scheduler_test_support.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_scheduler_cancellation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace CpuTaskSchedulerTestSupport;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskSchedulerTests, QueuedPrioritiesRunCriticalThenNormalThenBackgroundAfterOccupiedWorkerReleases){
    using namespace __hidden_cpu_task_scheduler_cancellation_tests;
    DeadlineGuard deadline;
    TaskGate occupied;
    CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
    ScopeExit releaseWorker([&]()noexcept{ occupied.open(); });
    const auto cpuTaskAdmission = scheduler.submit([&]()noexcept{ occupied.block(); });
    EXPECT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        EXPECT_TRUE(cpuTaskAdmission->valid());
    EXPECT_TRUE(WaitUntil(occupied.entered, s_GateTimeoutMS));
    Atomic<u32> cursor{ 0u };
    Array<u32, 3u> order{};
    const Array<CpuTaskPriority::Enum, 3u> priorities = { CpuTaskPriority::Background, CpuTaskPriority::Normal, CpuTaskPriority::Critical };
    for(u32 index = 0u; index < 3u; ++index){
        CpuTaskOptions options;
        options.priority = priorities[index];
        const auto secondCpuTaskAdmission = scheduler.submit([&, index]()noexcept{ order[cursor.fetch_add(1u)] = index; }, options);
        EXPECT_TRUE(secondCpuTaskAdmission);
        if(secondCpuTaskAdmission)
            EXPECT_TRUE(secondCpuTaskAdmission->valid());
    }
    occupied.open();
    scheduler.wait();
    EXPECT_FALSE(occupied.expired.load());
    EXPECT_EQ(cursor.load(), 3u);
    EXPECT_EQ(order[0u], 2u);
    EXPECT_EQ(order[1u], 1u);
    EXPECT_EQ(order[2u], 0u);
}


TEST(CpuTaskSchedulerTests, CanceledScopeSkipsQueuedBodiesAndTheirDependentsAndRetiresCaptures){
    using namespace __hidden_cpu_task_scheduler_cancellation_tests;
    DeadlineGuard deadline;
    Atomic<u32> retired{ 0u };
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 invoked = 0u;
    const auto predecessorTaskAdmission = scope.submit([&invoked, capture = LifetimeProbe(retired, 1u)]()noexcept{ invoked += capture.value; });
    ASSERT_TRUE(predecessorTaskAdmission);
    const auto predecessor = *predecessorTaskAdmission;
    const auto dependentTaskAdmission = scheduler.submit([&invoked, capture = LifetimeProbe(retired, 2u)]()noexcept{ invoked += capture.value; }, predecessor);
    ASSERT_TRUE(dependentTaskAdmission);
    const auto dependent = *dependentTaskAdmission;
    scope.cancel();
    const auto cpuTaskAdmission = scope.submit([&]()noexcept{ ++invoked; });
    EXPECT_FALSE(cpuTaskAdmission);
    scheduler.wait(dependent);
    scope.wait();
    EXPECT_EQ(invoked, 0u);
    EXPECT_EQ(retired.load(), 2u);
    EXPECT_EQ(scheduler.statistics().canceledTasks, 2u);
    EXPECT_EQ(scheduler.statistics().completedTasks, 0u);
}


TEST(CpuTaskSchedulerTests, CancelingAnActiveParentPreventsFutureDescendantsAndContinuations){
    using namespace __hidden_cpu_task_scheduler_cancellation_tests;
    DeadlineGuard deadline;
    TaskGate parentGate;
    CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
    CpuTaskScope scope(scheduler);
    ScopeExit releaseParent([&]()noexcept{ parentGate.open(); });
    Atomic<u32> childInvocations{ 0u };
    Atomic<u32> continuationInvocations{ 0u };
    Atomic<b8> childRejected{ false };
    const auto cpuTaskAdmission = scope.submit([&](){
        parentGate.block();
        const auto secondCpuTaskAdmission = scheduler.submit([&]()noexcept{ childInvocations.fetch_add(1u); });
        if(!secondCpuTaskAdmission || !secondCpuTaskAdmission->valid())
            childRejected.store(true, MemoryOrder::release);
    });
    ASSERT_TRUE(cpuTaskAdmission);
    const auto parent = *cpuTaskAdmission;
    EXPECT_TRUE(WaitUntil(parentGate.entered, s_GateTimeoutMS));
    const auto continuationTaskAdmission = scheduler.submit([&]()noexcept{ continuationInvocations.fetch_add(1u); }, parent);
    ASSERT_TRUE(continuationTaskAdmission);
    const auto continuation = *continuationTaskAdmission;
    scope.cancel();
    parentGate.open();
    scheduler.wait(continuation);
    scope.wait();
    EXPECT_FALSE(parentGate.expired.load());
    EXPECT_EQ(childInvocations.load(), 0u);
    EXPECT_EQ(continuationInvocations.load(), 0u);
    EXPECT_GE(scheduler.statistics().canceledTasks, 2u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskSchedulerTests, HeavyWorkUsesTheOtherClassWhileItsPreferredWorkerIsOccupied){
    using namespace __hidden_cpu_task_scheduler_cancellation_tests;
    DeadlineGuard deadline;
    TaskGate preferredGate;
    TaskGate spillGate;
    CpuTaskScheduler scheduler(2u);
    ScopeExit releaseWorkers([&]()noexcept{ preferredGate.open(); spillGate.open(); });
    const auto workers = scheduler.statistics();
    if(workers.performanceWorkers == 0u || workers.efficiencyWorkers == 0u)
        GTEST_SKIP() << "The permitted CPUs do not provide both capacity classes.";
    CpuAffinity::Enum preferredAffinity = CpuAffinity::Any;
    CpuAffinity::Enum spillAffinity = CpuAffinity::Any;
    const auto cpuTaskAdmission = scheduler.submit([&]()noexcept{
        preferredAffinity = scheduler.currentWorkerAffinity();
        preferredGate.block();
    });
    EXPECT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        EXPECT_TRUE(cpuTaskAdmission->valid());
    EXPECT_TRUE(WaitUntil(preferredGate.entered, s_GateTimeoutMS));
    const auto secondCpuTaskAdmission = scheduler.submit([&]()noexcept{
        spillAffinity = scheduler.currentWorkerAffinity();
        spillGate.block();
    });
    EXPECT_TRUE(secondCpuTaskAdmission);
    if(secondCpuTaskAdmission)
        EXPECT_TRUE(secondCpuTaskAdmission->valid());
    EXPECT_TRUE(WaitUntil(spillGate.entered, s_GateTimeoutMS));
    preferredGate.open();
    spillGate.open();
    scheduler.wait();
    EXPECT_FALSE(preferredGate.expired.load());
    EXPECT_FALSE(spillGate.expired.load());
    EXPECT_EQ(preferredAffinity, CpuAffinity::Performance);
    EXPECT_EQ(spillAffinity, CpuAffinity::Efficiency);
    EXPECT_GT(scheduler.statistics().performanceTasks, 0u);
    EXPECT_GT(scheduler.statistics().efficiencyTasks, 0u);
}

TEST(CpuTaskSchedulerTests, CanceledScopeSkipsBothDirectParallelRangeOverloads){
    using namespace __hidden_cpu_task_scheduler_cancellation_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 invocations = 0u;
    scope.cancel();
    EXPECT_TRUE((scope.parallelFor(0u, 17u, [&](usize)noexcept{ ++invocations; })));
    EXPECT_TRUE((scope.parallelFor(4u, 25u, 3u, [&](usize)noexcept{ ++invocations; })));
    scope.wait();
    EXPECT_EQ(invocations, 0u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

