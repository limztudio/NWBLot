// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpu_task_scheduler_test_support.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_scheduler_cancellation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_ThirdElementIndex = 2u;

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
    const auto cpuAdmission103 = scheduler.submit([&]()noexcept{ occupied.block(); });
    EXPECT_TRUE(cpuAdmission103);
    if(cpuAdmission103)
        EXPECT_TRUE(cpuAdmission103->valid());
    EXPECT_TRUE(CpuTaskTestSupport::WaitUntil(occupied.entered, s_GateTimeoutMS));
    Atomic<u32> cursor{ 0u };
    u32 order[3u]{};
    const CpuTaskPriority::Enum priorities[3u] = { CpuTaskPriority::Background, CpuTaskPriority::Normal, CpuTaskPriority::Critical };
    for(u32 index = 0u; index < 3u; ++index){
        CpuTaskOptions options;
        options.priority = priorities[index];
        const auto cpuAdmission104 = scheduler.submit([&, index]()noexcept{ order[cursor.fetch_add(1u)] = index; }, options);
        EXPECT_TRUE(cpuAdmission104);
        if(cpuAdmission104)
            EXPECT_TRUE(cpuAdmission104->valid());
    }
    occupied.open();
    scheduler.wait();
    EXPECT_FALSE(occupied.expired.load());
    EXPECT_EQ(cursor.load(), 3u);
    EXPECT_EQ(order[0u], s_ExpectedDualCount);
    EXPECT_EQ(order[1u], 1u);
    EXPECT_EQ(order[s_ThirdElementIndex], 0u);
}


TEST(CpuTaskSchedulerTests, CanceledScopeSkipsQueuedBodiesAndTheirDependentsAndRetiresCaptures){
    using namespace __hidden_cpu_task_scheduler_cancellation_tests;
    DeadlineGuard deadline;
    Atomic<u32> retired{ 0u };
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 invoked = 0u;
    const auto cpuAdmission105 = scope.submit([&invoked, capture = LifetimeProbe(retired, 1u)]()noexcept{ invoked += capture.value; });
    ASSERT_TRUE(cpuAdmission105);
    const auto predecessor = *cpuAdmission105;
    const auto cpuAdmission106 = scheduler.submit([&invoked, capture = LifetimeProbe(retired, s_ExpectedDualCount)]()noexcept{ invoked += capture.value; }, predecessor);
    ASSERT_TRUE(cpuAdmission106);
    const auto dependent = *cpuAdmission106;
    scope.cancel();
    const auto cpuAdmission107 = scope.submit([&]()noexcept{ ++invoked; });
    EXPECT_FALSE(cpuAdmission107);
    scheduler.wait(dependent);
    scope.wait();
    EXPECT_EQ(invoked, 0u);
    EXPECT_EQ(retired.load(), s_ExpectedDualCount);
    EXPECT_EQ(scheduler.statistics().canceledTasks, s_ExpectedDualCount);
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
    Atomic<bool> childRejected{ false };
    const auto cpuAdmission108 = scope.submit([&](){
        parentGate.block();
        const auto cpuAdmission109 = scheduler.submit([&]()noexcept{ childInvocations.fetch_add(1u); });
        if(!cpuAdmission109 || !cpuAdmission109->valid())
            childRejected.store(true, MemoryOrder::release);
    }
    );
    ASSERT_TRUE(cpuAdmission108);
    const auto parent = *cpuAdmission108;
    EXPECT_TRUE(CpuTaskTestSupport::WaitUntil(parentGate.entered, s_GateTimeoutMS));
    const auto cpuAdmission110 = scheduler.submit([&]()noexcept{ continuationInvocations.fetch_add(1u); }, parent);
    ASSERT_TRUE(cpuAdmission110);
    const auto continuation = *cpuAdmission110;
    scope.cancel();
    parentGate.open();
    scheduler.wait(continuation);
    scope.wait();
    EXPECT_FALSE(parentGate.expired.load());
    EXPECT_EQ(childInvocations.load(), 0u);
    EXPECT_EQ(continuationInvocations.load(), 0u);
    EXPECT_GE(scheduler.statistics().canceledTasks, s_ExpectedDualCount);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskSchedulerTests, HeavyWorkUsesTheOtherClassWhileItsPreferredWorkerIsOccupied){
    using namespace __hidden_cpu_task_scheduler_cancellation_tests;
    DeadlineGuard deadline;
    TaskGate preferredGate;
    TaskGate spillGate;
    CpuTaskScheduler scheduler(s_ExpectedDualCount);
    ScopeExit releaseWorkers([&]()noexcept{ preferredGate.open(); spillGate.open(); });
    const auto workers = scheduler.statistics();
    if(workers.performanceWorkers == 0u || workers.efficiencyWorkers == 0u)
        GTEST_SKIP() << "The permitted CPUs do not provide both capacity classes.";
    CpuAffinity::Enum preferredAffinity = CpuAffinity::Any;
    CpuAffinity::Enum spillAffinity = CpuAffinity::Any;
    const auto cpuAdmission111 = scheduler.submit([&]()noexcept{
        preferredAffinity = scheduler.currentWorkerAffinity();
        preferredGate.block();
    }
    );
    EXPECT_TRUE(cpuAdmission111);
    if(cpuAdmission111)
        EXPECT_TRUE(cpuAdmission111->valid());
    EXPECT_TRUE(CpuTaskTestSupport::WaitUntil(preferredGate.entered, s_GateTimeoutMS));
    const auto cpuAdmission112 = scheduler.submit([&]()noexcept{
        spillAffinity = scheduler.currentWorkerAffinity();
        spillGate.block();
    }
    );
    EXPECT_TRUE(cpuAdmission112);
    if(cpuAdmission112)
        EXPECT_TRUE(cpuAdmission112->valid());
    EXPECT_TRUE(CpuTaskTestSupport::WaitUntil(spillGate.entered, s_GateTimeoutMS));
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


TEST(CpuTaskSchedulerTests, DirectParallelRangeChildrenParticipateInTheReceivingScopesCancellation){
    using namespace __hidden_cpu_task_scheduler_cancellation_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 invocations = 0u;
    EXPECT_TRUE((scope.parallelFor(0u, 64u, 1u, [&](usize index)noexcept{
        ++invocations;
        if(index == 0u)
            scope.cancel();
    }
    )));
    scope.wait();
    EXPECT_GT(invocations, 0u);
    EXPECT_LT(invocations, 64u);
    EXPECT_GT(scheduler.statistics().canceledTasks, 0u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskSchedulerTests, LateDependenciesRetainCanceledResultsAfterRepeatedNodeReuse){
    using namespace __hidden_cpu_task_scheduler_cancellation_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope canceledScope(scheduler);
    u32 canceledInvocations = 0u;
    u32 unrelatedInvocations = 0u;
    const auto cpuAdmission117 = canceledScope.submit([&]()noexcept{ ++canceledInvocations; });
    ASSERT_TRUE(cpuAdmission117);
    const auto canceled = *cpuAdmission117;
    canceledScope.cancel();
    canceledScope.wait();
    for(u32 iteration = 0u; iteration < 16u; ++iteration){
        const auto cpuAdmission118 = scheduler.submit([&]()noexcept{ ++unrelatedInvocations; });
        ASSERT_TRUE(cpuAdmission118);
        const auto unrelated = *cpuAdmission118;
        scheduler.wait(unrelated);
    }
    const auto cpuAdmission119 = scheduler.submit([&]()noexcept{ ++canceledInvocations; }, canceled);
    ASSERT_TRUE(cpuAdmission119);
    const auto lateDependent = *cpuAdmission119;
    ASSERT_TRUE(lateDependent.valid());
    scheduler.wait(lateDependent);
    const auto cpuAdmission120 = scheduler.submit([&]()noexcept{ ++canceledInvocations; }, lateDependent);
    ASSERT_TRUE(cpuAdmission120);
    const auto laterDependent = *cpuAdmission120;
    ASSERT_TRUE(laterDependent.valid());
    scheduler.wait(laterDependent);
    EXPECT_EQ(canceledInvocations, 0u);
    EXPECT_EQ(unrelatedInvocations, 16u);
    EXPECT_EQ(scheduler.statistics().canceledTasks, 3u);
    EXPECT_EQ(scheduler.statistics().completedTasks, 16u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

