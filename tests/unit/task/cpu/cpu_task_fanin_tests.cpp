// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/cpu_task.h>
#include <core/alloc/scratch.h>

#include <global/platform.h>
#include <global/termination.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_fanin_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

inline constexpr u32 s_GateTimeoutMS = 20000u;
inline constexpr u32 s_TestTimeoutMS = 60000u;


class DeadlineGuard final{
public:
    DeadlineGuard()
        : m_watchdog([](const StopToken& stop){
            const Timer begin = TimerNow();
            while(!stop.stop_requested()){
                if(DurationInMS<u64>(TimerNow(), begin) >= s_TestTimeoutMS)
                    TerminateInvariant();
                SleepMS(1u);
            }
        })
    {}
    ~DeadlineGuard() = default;


private:
    JoiningThread m_watchdog;
};


[[nodiscard]] bool WaitUntil(const Atomic<bool>& condition){
    const Timer begin = TimerNow();
    while(!condition.load(MemoryOrder::acquire)){
        if(DurationInMS<u64>(TimerNow(), begin) >= s_GateTimeoutMS)
            return false;
        SleepMS(1u);
    }
    return true;
}


void VerifyFanIn(Alloc::ScratchArena& scratchArena, const usize uniqueDependencyCount, const usize repetitionCount){
    DeadlineGuard deadline;
    Atomic<bool> rootEntered{ false };
    Atomic<bool> releaseRoot{ false };
    Atomic<bool> rootExpired{ false };
    Atomic<u32> dependencyCompletionCount{ 0u };
    Atomic<u32> completionCountObservedByJoin{ 0u };
    Atomic<u32> joinInvocationCount{ 0u };
    Vector<u32, Alloc::ScratchArena> dependencyInvocations(uniqueDependencyCount, 0u, scratchArena);
    CpuTaskSchedulerConfig config;
    config.workerCount = 1u;
    config.heterogeneous = false;
    CpuTaskScheduler scheduler(config);
    ScopeExit releaseGuard([&]()noexcept{ releaseRoot.store(true, MemoryOrder::release); });

    const CpuTaskHandle completed = scheduler.submit([]()noexcept{});
    ASSERT_TRUE(completed.valid());
    scheduler.wait(completed);

    const CpuTaskHandle root = scheduler.submit([&](){
        rootEntered.store(true, MemoryOrder::release);
        if(!WaitUntil(releaseRoot))
            rootExpired.store(true, MemoryOrder::release);
    });
    ASSERT_TRUE(root.valid());
    ASSERT_TRUE(WaitUntil(rootEntered));
    EXPECT_TRUE(scheduler.isComplete(completed));
    EXPECT_FALSE(scheduler.isComplete(root));

    Vector<CpuTaskHandle, Alloc::ScratchArena> tasks(scratchArena);
    Vector<CpuTaskHandle, Alloc::ScratchArena> dependencies(scratchArena);
    tasks.reserve(uniqueDependencyCount);
    dependencies.reserve(uniqueDependencyCount * repetitionCount + 2u);
    for(usize index = 0u; index < uniqueDependencyCount; ++index){
        const CpuTaskHandle dependency = scheduler.submit([&, index](){
            ++dependencyInvocations[index];
            dependencyCompletionCount.fetch_add(1u, MemoryOrder::release);
        }, root);
        ASSERT_TRUE(dependency.valid());
        tasks.push_back(dependency);
    }

    // An odd stride permutes the power-of-two task counts, separating repeated edges by a full pass.
    for(usize repeat = 0u; repeat < repetitionCount; ++repeat){
        for(usize index = 0u; index < uniqueDependencyCount; ++index)
            dependencies.push_back(tasks[(index * 4051u + repeat) % uniqueDependencyCount]);
    }
    // Retired generations and default handles add no pending prerequisite after node storage is reused.
    dependencies.push_back(completed);
    dependencies.push_back(CpuTaskHandle{});

    const CpuTaskHandle joined = scheduler.submit([&](){
        completionCountObservedByJoin.store(dependencyCompletionCount.load(MemoryOrder::acquire), MemoryOrder::release);
        joinInvocationCount.fetch_add(1u, MemoryOrder::release);
    }, {}, dependencies.data(), dependencies.size());
    ASSERT_TRUE(joined.valid());
    EXPECT_FALSE(scheduler.isComplete(joined));
    EXPECT_EQ(dependencyCompletionCount.load(MemoryOrder::acquire), 0u);
    EXPECT_EQ(joinInvocationCount.load(MemoryOrder::acquire), 0u);

    releaseRoot.store(true, MemoryOrder::release);
    scheduler.wait(joined);
    scheduler.wait();
    EXPECT_FALSE(rootExpired.load(MemoryOrder::acquire));
    EXPECT_EQ(dependencyCompletionCount.load(MemoryOrder::acquire), uniqueDependencyCount);
    EXPECT_EQ(completionCountObservedByJoin.load(MemoryOrder::acquire), uniqueDependencyCount);
    EXPECT_EQ(joinInvocationCount.load(MemoryOrder::acquire), 1u);
    for(const u32 invocations : dependencyInvocations)
        EXPECT_EQ(invocations, 1u);
    EXPECT_EQ(scheduler.statistics().completedTasks, uniqueDependencyCount + 3u);
    EXPECT_EQ(scheduler.statistics().canceledTasks, 0u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskSchedulerTests, LargeDistinctFanInWaitsForEveryDependency){
    NWB::Core::Alloc::ScratchArena scratchArena("CpuTaskSchedulerTests.LargeDistinctFanIn");
    __hidden_cpu_task_fanin_tests::VerifyFanIn(scratchArena, 4096u, 1u);
}

TEST(CpuTaskSchedulerTests, LargeRepeatedFanInPublishesExactlyOneJoin){
    NWB::Core::Alloc::ScratchArena scratchArena("CpuTaskSchedulerTests.LargeRepeatedFanIn");
    __hidden_cpu_task_fanin_tests::VerifyFanIn(scratchArena, 1024u, 4u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

