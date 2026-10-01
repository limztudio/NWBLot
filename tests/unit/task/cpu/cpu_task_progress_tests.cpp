// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/cpu/scheduler.h>

#include <global/termination.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_progress_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

inline constexpr u32 s_Rounds = 16u;
inline constexpr u32 s_Workers = 2u;
inline constexpr u32 s_RootCount = 32u;
inline constexpr u32 s_TimeoutMS = 30000u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Deadline final{
public:
    Deadline()
        : m_watchdog([](const StopToken& stop){
            const Timer begin = TimerNow();
            while(!stop.stop_requested()){
                if(DurationInMS<u64>(TimerNow(), begin) >= s_TimeoutMS)
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


TEST(CpuTaskProgressTests, ScopeJoinWakesForANewlyReadyUnscopedMainThreadPrerequisite){
    using namespace __hidden_cpu_task_progress_tests;
    Deadline deadline;
    CpuTaskSchedulerConfig config;
    config.workerCount = 1u;
    config.heterogeneous = false;
    CpuTaskScheduler scheduler(config);
    constexpr CpuTaskOptions s_MainThread{ .target = CpuTaskTarget::MainThread };
    constexpr u32 s_CallbackCount = 2u;
    constexpr u32 s_ReleaseSchedules = 2u;
    for(u32 round = 0u; round < s_Rounds; ++round){
        Atomic<bool> release{ false };
        Atomic<bool> joining{ false };
        Atomic<bool> workerEntered{ false };
        u32 callbacks = 0u;
        CpuTaskScope scope(scheduler);
        JoiningThread releaser;
        ScopeExit finish([&]()noexcept{
            joining.store(true, MemoryOrder::release);
            joining.notify_all();
            release.store(true, MemoryOrder::release);
            release.notify_all();
            scheduler.drain();
            if(releaser.joinable())
                releaser.join();
        });
        const auto gate = scheduler.submit([&](){
            workerEntered.store(true, MemoryOrder::release);
            workerEntered.notify_one();
            release.wait(false, MemoryOrder::acquire);
        });
        ASSERT_TRUE(gate.valid());
        workerEntered.wait(false, MemoryOrder::acquire);
        const auto prerequisite = scheduler.submit([&](){ ++callbacks; }, s_MainThread, &gate, 1u);
        ASSERT_TRUE(prerequisite.valid());
        ASSERT_TRUE(scope.submit([&](){ ++callbacks; }, s_MainThread, &prerequisite, 1u).valid());
        releaser = JoiningThread([&](){
            joining.wait(false, MemoryOrder::acquire);
            if(round % s_ReleaseSchedules == 0u)
                SleepMS(1u);
            release.store(true, MemoryOrder::release);
            release.notify_one();
        });
        // No handle waiter or completed scope can provide the prerequisite's readiness notification.
        joining.store(true, MemoryOrder::release);
        joining.notify_one();
        scope.wait();
        releaser.join();
        scheduler.wait();
        finish.release();
        EXPECT_EQ(callbacks, s_CallbackCount);
        EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
    }
}

TEST(CpuTaskProgressTests, ConcurrentSameScopeJoinsRetainLatePublishedDescendantsAcrossScopeReuse){
    using namespace __hidden_cpu_task_progress_tests;
    Deadline deadline;
    CpuTaskSchedulerConfig config;
    config.workerCount = s_Workers;
    config.heterogeneous = false;
    CpuTaskScheduler scheduler(config);
    for(u32 round = 0u; round < s_Rounds; ++round){
        Atomic<bool> start{ false };
        Atomic<u32> entered{ 0u };
        Atomic<u32> visits{ 0u };
        Atomic<u32> joined{ 0u };
        CpuTaskScope scope(scheduler);
        CpuTaskHandle roots[s_RootCount];
        ScopeExit finish([&]()noexcept{
            start.store(true, MemoryOrder::release);
            start.notify_all();
            scheduler.drain();
        });
        for(u32 waiter = 0u; waiter < s_Workers; ++waiter){
            ASSERT_TRUE(scheduler.submit([&](){
                entered.fetch_add(1u, MemoryOrder::release);
                entered.notify_one();
                start.wait(false, MemoryOrder::acquire);
                scope.wait();
                joined.fetch_add(1u, MemoryOrder::release);
            }).valid());
        }
        for(u32 count = entered.load(MemoryOrder::acquire); count != s_Workers; count = entered.load(MemoryOrder::acquire))
            entered.wait(count, MemoryOrder::acquire);
        for(auto& root : roots){
            root = scheduler.submit([&](){ visits.fetch_add(1u, MemoryOrder::relaxed); });
            ASSERT_TRUE(root.valid());
        }
        ASSERT_TRUE(scope.submit([&](){
            EXPECT_TRUE(scheduler.submit([&](){
                EXPECT_TRUE(scheduler.submit([&](){ visits.fetch_add(1u, MemoryOrder::relaxed); }).valid());
            }).valid());
        }, {}, roots, s_RootCount).valid());
        start.store(true, MemoryOrder::release);
        start.notify_all();
        scheduler.wait();
        EXPECT_EQ(joined.load(MemoryOrder::acquire), s_Workers);
        EXPECT_EQ(visits.load(MemoryOrder::acquire), s_RootCount + 1u);
        // This scope keeps its identity after completion, while publication invalidates its old contribution proofs.
        const auto later = scheduler.submit([&](){ visits.fetch_add(1u, MemoryOrder::relaxed); });
        ASSERT_TRUE(later.valid());
        ASSERT_TRUE(scope.submit([&](){ visits.fetch_add(1u, MemoryOrder::relaxed); }, later).valid());
        scope.wait();
        scheduler.wait();
        finish.release();
        EXPECT_EQ(visits.load(MemoryOrder::acquire), s_RootCount + 3u);
        EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

