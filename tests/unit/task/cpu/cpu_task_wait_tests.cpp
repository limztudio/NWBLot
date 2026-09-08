// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/cpu/scheduler.h>

#include <global/termination.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_wait_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

class WaitDeadline final{
public:
    WaitDeadline()
        : m_watchdog([](const StopToken& stop){
            const Timer begin = TimerNow();
            while(!stop.stop_requested()){
                if(DurationInMS<u64>(TimerNow(), begin) >= 10000u)
                    TerminateInvariant();
                SleepMS(1u);
            }
        })
    {}


private:
    JoiningThread m_watchdog;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskWaitTests, ScopeRejectsAnIndependentTaskDependingOnTheCaller){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 callbacks = 0u;
    const auto caller = scheduler.submit([&](){ scope.wait(); });
    ASSERT_TRUE(caller.valid());
    ASSERT_TRUE(scope.submit([&](){ ++callbacks; }, caller).valid());
    EXPECT_THROW(scheduler.wait(caller), RuntimeException);
    scope.wait();
    EXPECT_EQ(callbacks, 0u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}

TEST(CpuTaskWaitTests, ScopeRejectsTransitiveDependentsOfTheCallersAncestor){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    const auto ancestor = scheduler.submit([&](){
        ASSERT_TRUE(scheduler.submit([&](){ scope.wait(); }).valid());
    });
    ASSERT_TRUE(ancestor.valid());
    const auto middle = scheduler.submit([](){}, ancestor);
    ASSERT_TRUE(middle.valid());
    ASSERT_TRUE(scope.submit([](){}, middle).valid());
    EXPECT_THROW(scheduler.wait(ancestor), RuntimeException);
    scope.wait();
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}

TEST(CpuTaskWaitTests, ScopeRechecksDependenciesPublishedByCooperativeWork){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 callbacks = 0u;
    const auto caller = scheduler.submit([&](){ scope.wait(); });
    ASSERT_TRUE(caller.valid());
    ASSERT_TRUE(scope.submit([&](){
        ++callbacks;
        ASSERT_TRUE(scope.submit([&](){ ++callbacks; }, caller).valid());
    }).valid());
    EXPECT_THROW(scheduler.wait(caller), RuntimeException);
    scope.wait();
    EXPECT_EQ(callbacks, 1u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}

TEST(CpuTaskWaitTests, ScopeCanJoinIndependentPrerequisitesAndDescendants){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 callbacks = 0u;
    const auto caller = scheduler.submit([&](){ scope.wait(); });
    ASSERT_TRUE(caller.valid());
    const auto prerequisite = scheduler.submit([&](){ ++callbacks; });
    ASSERT_TRUE(prerequisite.valid());
    ASSERT_TRUE(scope.submit([&](){
        ++callbacks;
        ASSERT_TRUE(scheduler.submit([&](){ ++callbacks; }).valid());
    }, prerequisite).valid());
    scheduler.wait(caller);
    scope.wait();
    EXPECT_EQ(callbacks, 3u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

