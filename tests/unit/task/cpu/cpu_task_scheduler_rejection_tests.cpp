// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpu_task_scheduler_test_support.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_scheduler_rejection_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;

using namespace CpuTaskSchedulerTestSupport;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskSchedulerTests, WorkerSelfWaitIsRejectedInsteadOfDeadlocking){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_scheduler_rejection_tests;
        DeadlineGuard deadline(true);
        TaskGate start;
        CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
        CpuTaskHandle self;
        const auto selfTaskAdmission = scheduler.submit([&](){ start.block(); scheduler.wait(self); });
        if(!selfTaskAdmission)
            ExitTestProcess(0u);
        self = *selfTaskAdmission;
        start.open();
        scheduler.wait();
    }, ""
    );
}


TEST(CpuTaskSchedulerTests, DescendantWaitForAnAncestorIsRejectedInsteadOfDeadlocking){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_scheduler_rejection_tests;
        DeadlineGuard deadline(true);
        TaskGate start;
        CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
        CpuTaskHandle parent;
        const auto cpuTaskAdmission = scheduler.submit([&](){
            start.block();
            const auto childTaskAdmission = scheduler.submit([&](){ scheduler.wait(parent); });
            if(!childTaskAdmission)
                ExitTestProcess(0u);
            const auto child = *childTaskAdmission;
            EXPECT_TRUE(child.valid());
        }
        );
        if(!cpuTaskAdmission)
            ExitTestProcess(0u);
        parent = *cpuTaskAdmission;
        start.open();
        scheduler.wait();
    }, ""
    );
}


TEST(CpuTaskSchedulerTests, JoiningTheScopeThatContainsTheCurrentTaskIsRejected){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_scheduler_rejection_tests;
        DeadlineGuard deadline(true);
        CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
        CpuTaskScope scope(scheduler);
        const auto cpuTaskAdmission = scope.submit([&](){ scope.wait(); });
        EXPECT_TRUE(cpuTaskAdmission);
        if(cpuTaskAdmission)
            EXPECT_TRUE(cpuTaskAdmission->valid());
        scheduler.wait();
    }, ""
    );
}

TEST(CpuTaskSchedulerTests, ChildCannotDependDirectlyOnItsStructuredParent){
    EXPECT_EXIT({
        using namespace __hidden_cpu_task_scheduler_rejection_tests;
        DeadlineGuard deadline;
        CpuTaskScheduler scheduler(0u);
        CpuTaskHandle parent;
        bool rejected = false;
        const auto cpuTaskAdmission = scheduler.submit([&](){
            const auto child = scheduler.submit([]()noexcept{}, parent);
            rejected = !child;
        }
        );
        if(!cpuTaskAdmission)
            ExitTestProcess(2u);
        parent = *cpuTaskAdmission;
        scheduler.pumpMainThread();
        if(!rejected)
            ExitTestProcess(1u);
        scheduler.wait(parent);
        ExitTestProcess(scheduler.statistics().outstandingTasks == 0u ? 0u : s_ExpectedDualCount);
    }, testing::ExitedWithCode(0), ""
    );
}


TEST(CpuTaskSchedulerTests, ChildCannotDependOnATransitiveDependentOfItsStructuredParent){
    EXPECT_EXIT({
        using namespace __hidden_cpu_task_scheduler_rejection_tests;
        DeadlineGuard deadline;
        CpuTaskScheduler scheduler(0u);
        CpuTaskHandle dependent;
        bool rejected = false;
        u32 continuations = 0u;
        const auto cpuTaskAdmission = scheduler.submit([&](){
            const auto child = scheduler.submit([]()noexcept{}, dependent);
            rejected = !child;
        }
        );
        if(!cpuTaskAdmission)
            ExitTestProcess(2u);
        const auto parent = *cpuTaskAdmission;
        dependent = parent;
        for(u32 depth = 0u; depth < 3u; ++depth){
            const auto dependentTaskAdmission = scheduler.submit([&]()noexcept{ ++continuations; }, dependent);
            if(!dependentTaskAdmission)
                ExitTestProcess(2u);
            dependent = *dependentTaskAdmission;
        }
        scheduler.pumpMainThread();
        if(!rejected)
            ExitTestProcess(1u);
        scheduler.wait(dependent);
        ExitTestProcess(continuations == 3u && scheduler.statistics().outstandingTasks == 0u ? 0u : s_ExpectedDualCount);
    }, testing::ExitedWithCode(0), ""
    );
}


TEST(CpuTaskSchedulerTests, DescendantOnAnotherWorkerCannotJoinItsAncestorsScope){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_scheduler_rejection_tests;
        DeadlineGuard deadline(true);
        TaskGate parentGate;
        CpuTaskScheduler scheduler(HomogeneousWorkers(s_ExpectedDualCount));
        CpuTaskScope scope(scheduler);
        Atomic<usize> parentWorker{ 0u };
        const auto cpuTaskAdmission = scope.submit([&](){
            parentWorker.store(scheduler.currentWorkerIndex(), MemoryOrder::release);
            const auto secondCpuTaskAdmission = scheduler.submit([&](){
                if(!CpuTaskTestSupport::WaitUntil(parentGate.entered, s_GateTimeoutMS) || scheduler.currentWorkerIndex() == parentWorker.load(MemoryOrder::acquire))
                    ExitTestProcess(0u);
                scope.wait();
            }
            );
            EXPECT_TRUE(secondCpuTaskAdmission);
            if(secondCpuTaskAdmission)
                EXPECT_TRUE(secondCpuTaskAdmission->valid());
            parentGate.block();
        }
        );
        EXPECT_TRUE(cpuTaskAdmission);
        if(cpuTaskAdmission)
            EXPECT_TRUE(cpuTaskAdmission->valid());
        scheduler.wait();
    }, ""
    );
}


TEST(CpuTaskSchedulerTests, WorkerCannotWaitForAnIndependentlySubmittedDependent){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_scheduler_rejection_tests;
        DeadlineGuard deadline(true);
        TaskGate start;
        CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
        CpuTaskHandle dependent;
        const auto runningTaskAdmission = scheduler.submit([&](){ start.block(); scheduler.wait(dependent); });
        if(!runningTaskAdmission)
            ExitTestProcess(0u);
        const auto running = *runningTaskAdmission;
        const auto dependentTaskAdmission = scheduler.submit([]()noexcept{}, running);
        if(!dependentTaskAdmission)
            ExitTestProcess(0u);
        dependent = *dependentTaskAdmission;
        start.open();
        scheduler.wait();
    }, ""
    );
}


TEST(CpuTaskSchedulerTests, DescendantCannotWaitForTransitiveDependentsOfItsAncestor){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_scheduler_rejection_tests;
        DeadlineGuard deadline(true);
        TaskGate start;
        CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
        CpuTaskHandle dependent;
        const auto cpuTaskAdmission = scheduler.submit([&](){
            start.block();
            const auto secondCpuTaskAdmission = scheduler.submit([&](){ scheduler.wait(dependent); });
            EXPECT_TRUE(secondCpuTaskAdmission);
            if(secondCpuTaskAdmission)
                EXPECT_TRUE(secondCpuTaskAdmission->valid());
        }
        );
        if(!cpuTaskAdmission)
            ExitTestProcess(0u);
        const auto parent = *cpuTaskAdmission;
        dependent = parent;
        for(u32 depth = 0u; depth < 3u; ++depth){
            const auto dependentTaskAdmission = scheduler.submit([]()noexcept{}, dependent);
            if(!dependentTaskAdmission)
                ExitTestProcess(0u);
            dependent = *dependentTaskAdmission;
        }
        start.open();
        scheduler.wait();
    }, ""
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

