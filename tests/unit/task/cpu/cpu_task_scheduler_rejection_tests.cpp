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
        const auto cpuAdmission113 = scheduler.submit([&](){ start.block(); scheduler.wait(self); });
        if(!cpuAdmission113)
            ExitTestProcess(0u);
        self = *cpuAdmission113;
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
        const auto cpuAdmission114 = scheduler.submit([&](){
            start.block();
            const auto cpuAdmission115 = scheduler.submit([&](){ scheduler.wait(parent); });
            if(!cpuAdmission115)
                ExitTestProcess(0u);
            const auto child = *cpuAdmission115;
            EXPECT_TRUE(child.valid());
        }
        );
        if(!cpuAdmission114)
            ExitTestProcess(0u);
        parent = *cpuAdmission114;
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
        const auto cpuAdmission116 = scope.submit([&](){ scope.wait(); });
        EXPECT_TRUE(cpuAdmission116);
        if(cpuAdmission116)
            EXPECT_TRUE(cpuAdmission116->valid());
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
        const auto cpuAdmission121 = scheduler.submit([&](){
            const auto child = scheduler.submit([]()noexcept{}, parent);
            rejected = !child;
        }
        );
        if(!cpuAdmission121)
            ExitTestProcess(2u);
        parent = *cpuAdmission121;
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
        const auto cpuAdmission123 = scheduler.submit([&](){
            const auto child = scheduler.submit([]()noexcept{}, dependent);
            rejected = !child;
        }
        );
        if(!cpuAdmission123)
            ExitTestProcess(2u);
        const auto parent = *cpuAdmission123;
        dependent = parent;
        for(u32 depth = 0u; depth < 3u; ++depth){
            const auto cpuAdmission125 = scheduler.submit([&]()noexcept{ ++continuations; }, dependent);
            if(!cpuAdmission125)
                ExitTestProcess(2u);
            dependent = *cpuAdmission125;
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
        const auto cpuAdmission126 = scope.submit([&](){
            parentWorker.store(scheduler.currentWorkerIndex(), MemoryOrder::release);
            const auto cpuAdmission127 = scheduler.submit([&](){
                if(!CpuTaskTestSupport::WaitUntil(parentGate.entered, s_GateTimeoutMS) || scheduler.currentWorkerIndex() == parentWorker.load(MemoryOrder::acquire))
                    ExitTestProcess(0u);
                scope.wait();
            }
            );
            EXPECT_TRUE(cpuAdmission127);
            if(cpuAdmission127)
                EXPECT_TRUE(cpuAdmission127->valid());
            parentGate.block();
        }
        );
        EXPECT_TRUE(cpuAdmission126);
        if(cpuAdmission126)
            EXPECT_TRUE(cpuAdmission126->valid());
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
        const auto cpuAdmission128 = scheduler.submit([&](){ start.block(); scheduler.wait(dependent); });
        if(!cpuAdmission128)
            ExitTestProcess(0u);
        const auto running = *cpuAdmission128;
        const auto cpuAdmission129 = scheduler.submit([]()noexcept{}, running);
        if(!cpuAdmission129)
            ExitTestProcess(0u);
        dependent = *cpuAdmission129;
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
        const auto cpuAdmission130 = scheduler.submit([&](){
            start.block();
            const auto cpuAdmission131 = scheduler.submit([&](){ scheduler.wait(dependent); });
            EXPECT_TRUE(cpuAdmission131);
            if(cpuAdmission131)
                EXPECT_TRUE(cpuAdmission131->valid());
        }
        );
        if(!cpuAdmission130)
            ExitTestProcess(0u);
        const auto parent = *cpuAdmission130;
        dependent = parent;
        for(u32 depth = 0u; depth < 3u; ++depth){
            const auto cpuAdmission132 = scheduler.submit([]()noexcept{}, dependent);
            if(!cpuAdmission132)
                ExitTestProcess(0u);
            dependent = *cpuAdmission132;
        }
        start.open();
        scheduler.wait();
    }, ""
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

