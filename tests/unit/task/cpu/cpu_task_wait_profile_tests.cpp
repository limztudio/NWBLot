// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/cpu/scheduler.h>
#include <core/alloc/scratch.h>

#include <global/termination.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_wait_profile_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

inline constexpr usize s_TaskCount = 4096u;
inline constexpr usize s_WaitScopeCount = 4u;
inline constexpr u32 s_Warmups = 2u;
inline constexpr u32 s_Samples = 8u;
inline constexpr u32 s_TimeoutMS = 60000u;
inline constexpr u32 s_ContributionScopeCount = 2u;
inline constexpr usize s_ContributionRoots = 512u;
inline constexpr u32 s_GlobalJoin = 0u;
inline constexpr u32 s_SharedScopeJoin = 1u;
inline constexpr u32 s_DifferentScopeJoins = 2u;
inline constexpr AStringView s_SampleKeys[s_Samples] = {
    "sample_0_ns", "sample_1_ns", "sample_2_ns", "sample_3_ns",
    "sample_4_ns", "sample_5_ns", "sample_6_ns", "sample_7_ns"
};


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


void RecordUnsigned(const AStringView key, const u64 value){
    constexpr usize s_TextCapacity = 32u;
    char text[s_TextCapacity] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(AInteropString(key), text);
}

void RecordSample(const u32 sample, const u64 nanoseconds, const u64 checksum){
    if(sample < s_Warmups)
        return;
    RecordUnsigned(s_SampleKeys[sample - s_Warmups], nanoseconds);
    RecordUnsigned("checksum", checksum);
}

[[nodiscard]] u64 Work(u64 value)noexcept{
    constexpr u32 s_Rounds = 16u;
    for(u32 round = 0u; round < s_Rounds; ++round){
        value ^= value >> 17u;
        value *= 0x9e3779b185ebca87ull;
        value ^= value >> 29u;
        value += 0xd1b54a32d192ed03ull;
    }
    return value;
}

void ProfileWaiters(Alloc::ScratchArena& scratch, const u32 workers, const u32 waiterCount){
    Vector<u64, Alloc::ScratchArena> output(s_TaskCount, 0u, scratch);
    Vector<u64, Alloc::ScratchArena> expected(s_TaskCount, 0u, scratch);
    for(usize index = 0u; index < s_TaskCount; ++index)
        expected[index] = Work(index + 1u);
    CpuTaskSchedulerConfig config;
    config.workerCount = workers;
    config.heterogeneous = false;
    CpuTaskScheduler scheduler(config);
    RecordUnsigned("worker_count", workers);
    RecordUnsigned("scope_waiters", waiterCount);
    RecordUnsigned("ready_tasks", s_TaskCount);
    for(u32 sample = 0u; sample < s_Warmups + s_Samples; ++sample){
        for(u64& value : output)
            value = 0u;
        Atomic<bool> start{ false };
        Atomic<u32> workersEntered{ 0u };
        Atomic<u32> waitersEntered{ 0u };
        CpuTaskScope scopes[s_WaitScopeCount] = {
            CpuTaskScope(scheduler), CpuTaskScope(scheduler), CpuTaskScope(scheduler), CpuTaskScope(scheduler)
        };
        Vector<JoiningThread, Alloc::ScratchArena> waiters(scratch);
        waiters.reserve(waiterCount);
        ScopeExit cleanup([&]()noexcept{
            start.store(true, MemoryOrder::release);
            start.notify_all();
            scheduler.drain();
            for(auto& waiter : waiters){
                if(waiter.joinable())
                    waiter.join();
            }
        });
        for(u32 worker = 0u; worker < workers; ++worker){
            ASSERT_TRUE(scheduler.submit([&](){
                workersEntered.fetch_add(1u, MemoryOrder::release);
                workersEntered.notify_one();
                start.wait(false, MemoryOrder::acquire);
            }).valid());
        }
        for(u32 count = workersEntered.load(MemoryOrder::acquire); count != workers; count = workersEntered.load(MemoryOrder::acquire))
            workersEntered.wait(count, MemoryOrder::acquire);
        for(usize index = 0u; index < s_TaskCount; ++index){
            ASSERT_TRUE(scopes[index % s_WaitScopeCount].submit([&, index](){ output[index] = Work(index + 1u); }).valid());
        }
        for(u32 waiter = 0u; waiter < waiterCount; ++waiter){
            waiters.emplace_back([&, waiter](){
                waitersEntered.fetch_add(1u, MemoryOrder::release);
                waitersEntered.notify_one();
                scopes[waiter % s_WaitScopeCount].wait();
            });
        }
        for(u32 count = waitersEntered.load(MemoryOrder::acquire); count != waiterCount; count = waitersEntered.load(MemoryOrder::acquire))
            waitersEntered.wait(count, MemoryOrder::acquire);
        const Timer begin = TimerNow();
        start.store(true, MemoryOrder::release);
        start.notify_all();
        scheduler.wait();
        for(auto& waiter : waiters)
            waiter.join();
        const u64 nanoseconds = DurationInNS<u64>(TimerNow(), begin);
        cleanup.release();
        u64 checksum = 0u;
        for(usize index = 0u; index < s_TaskCount; ++index){
            ASSERT_EQ(output[index], expected[index]);
            checksum += output[index];
        }
        ASSERT_EQ(scheduler.statistics().outstandingTasks, 0u);
        RecordSample(sample, nanoseconds, checksum);
    }
}

[[nodiscard]] CpuTaskSchedulerConfig Config(const u32 workers){
    CpuTaskSchedulerConfig config;
    config.workerCount = workers;
    config.heterogeneous = false;
    return config;
}

void ProfileContributions(Alloc::ScratchArena& scratch, const u32 workers, const usize roots, const usize chain, const u32 joinMode){
    Vector<CpuTaskHandle, Alloc::ScratchArena> prerequisites(roots, CpuTaskHandle{}, scratch);
    Vector<u32, Alloc::ScratchArena> visits(roots, 0u, scratch);
    Atomic<u32> continuations{ 0u };
    Atomic<u32> leaves{ 0u };
    CpuTaskScheduler scheduler(Config(workers));
    RecordUnsigned("worker_count", workers);
    RecordUnsigned("root_prerequisites", roots);
    RecordUnsigned("continuation_chain", chain);
    RecordUnsigned("join_mode", joinMode);
    for(u32 sample = 0u; sample < s_Warmups + s_Samples; ++sample){
        for(u32& value : visits)
            value = 0u;
        continuations.store(0u, MemoryOrder::relaxed);
        leaves.store(0u, MemoryOrder::relaxed);
        Atomic<bool> start{ false };
        Atomic<u32> entered{ 0u };
        CpuTaskScope first(scheduler);
        CpuTaskScope second(scheduler);
        ScopeExit drainOnFailure([&]()noexcept{
            start.store(true, MemoryOrder::release);
            start.notify_all();
            scheduler.drain();
        });
        for(u32 waiter = 0u; waiter < workers; ++waiter){
            ASSERT_TRUE(scheduler.submit([&, waiter](){
                entered.fetch_add(1u, MemoryOrder::release);
                entered.notify_one();
                start.wait(false, MemoryOrder::acquire);
                if(joinMode != s_GlobalJoin){
                    if(joinMode == s_SharedScopeJoin || waiter % s_ContributionScopeCount == 0u)
                        first.wait();
                    else
                        second.wait();
                }
            }).valid());
        }
        for(u32 observed = entered.load(MemoryOrder::acquire); observed != workers; observed = entered.load(MemoryOrder::acquire))
            entered.wait(observed, MemoryOrder::acquire);
        for(usize index = 0u; index < roots; ++index){
            prerequisites[index] = scheduler.submit([&, index](){ ++visits[index]; });
            ASSERT_TRUE(prerequisites[index].valid());
        }
        auto tail = scheduler.submit(
            [&](){ continuations.fetch_add(1u, MemoryOrder::relaxed); }, {}, prerequisites.data(), roots
        );
        ASSERT_TRUE(tail.valid());
        for(usize index = 0u; index < chain; ++index){
            tail = scheduler.submit([&](){ continuations.fetch_add(1u, MemoryOrder::relaxed); }, tail);
            ASSERT_TRUE(tail.valid());
        }
        ASSERT_TRUE(first.submit([&](){ leaves.fetch_add(1u, MemoryOrder::relaxed); }, tail).valid());
        ASSERT_TRUE(second.submit([&](){ leaves.fetch_add(1u, MemoryOrder::relaxed); }, tail).valid());
        const Timer begin = TimerNow();
        start.store(true, MemoryOrder::release);
        start.notify_all();
        if(workers == 0u && joinMode != s_GlobalJoin){
            first.wait();
            second.wait();
        }
        scheduler.wait();
        const u64 nanoseconds = DurationInNS<u64>(TimerNow(), begin);
        drainOnFailure.release();
        for(const u32 value : visits)
            ASSERT_EQ(value, 1u);
        ASSERT_EQ(continuations.load(MemoryOrder::acquire), chain + 1u);
        ASSERT_EQ(leaves.load(MemoryOrder::acquire), s_ContributionScopeCount);
        ASSERT_EQ(scheduler.statistics().outstandingTasks, 0u);
        RecordSample(sample, nanoseconds, roots + chain + 1u + s_ContributionScopeCount);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskWaitProfile, DISABLED_BatchNoWorkersNoWaiters){
    using namespace __hidden_cpu_task_wait_profile_tests;
    Deadline deadline;
    Alloc::ScratchArena scratch("tests/task/cpu/wait_profile");
    ProfileWaiters(scratch, 0u, 0u);
}

TEST(CpuTaskWaitProfile, DISABLED_BatchNoWorkersOneWaiter){
    using namespace __hidden_cpu_task_wait_profile_tests;
    Deadline deadline;
    Alloc::ScratchArena scratch("tests/task/cpu/wait_profile");
    ProfileWaiters(scratch, 0u, 1u);
}

TEST(CpuTaskWaitProfile, DISABLED_BatchNoWorkersFourWaiters){
    using namespace __hidden_cpu_task_wait_profile_tests;
    Deadline deadline;
    Alloc::ScratchArena scratch("tests/task/cpu/wait_profile");
    ProfileWaiters(scratch, 0u, 4u);
}

TEST(CpuTaskWaitProfile, DISABLED_BatchNoWorkersSixteenWaiters){
    using namespace __hidden_cpu_task_wait_profile_tests;
    Deadline deadline;
    Alloc::ScratchArena scratch("tests/task/cpu/wait_profile");
    ProfileWaiters(scratch, 0u, 16u);
}

TEST(CpuTaskWaitProfile, DISABLED_BatchFourWorkersNoWaiters){
    using namespace __hidden_cpu_task_wait_profile_tests;
    Deadline deadline;
    Alloc::ScratchArena scratch("tests/task/cpu/wait_profile");
    ProfileWaiters(scratch, 4u, 0u);
}

TEST(CpuTaskWaitProfile, DISABLED_BatchFourWorkersOneWaiter){
    using namespace __hidden_cpu_task_wait_profile_tests;
    Deadline deadline;
    Alloc::ScratchArena scratch("tests/task/cpu/wait_profile");
    ProfileWaiters(scratch, 4u, 1u);
}

TEST(CpuTaskWaitProfile, DISABLED_BatchFourWorkersFourWaiters){
    using namespace __hidden_cpu_task_wait_profile_tests;
    Deadline deadline;
    Alloc::ScratchArena scratch("tests/task/cpu/wait_profile");
    ProfileWaiters(scratch, 4u, 4u);
}

TEST(CpuTaskWaitProfile, DISABLED_BatchFourWorkersSixteenWaiters){
    using namespace __hidden_cpu_task_wait_profile_tests;
    Deadline deadline;
    Alloc::ScratchArena scratch("tests/task/cpu/wait_profile");
    ProfileWaiters(scratch, 4u, 16u);
}

TEST(CpuTaskWaitProfile, DISABLED_SameScopeChain512TwoWorkers){
    using namespace __hidden_cpu_task_wait_profile_tests;
    Deadline deadline;
    Alloc::ScratchArena scratch("tests/task/cpu/wait_profile");
    ProfileContributions(scratch, 2u, s_ContributionRoots, 512u, s_SharedScopeJoin);
}

TEST(CpuTaskWaitProfile, DISABLED_SameScopeChain2048TwoWorkers){
    using namespace __hidden_cpu_task_wait_profile_tests;
    Deadline deadline;
    Alloc::ScratchArena scratch("tests/task/cpu/wait_profile");
    ProfileContributions(scratch, 2u, s_ContributionRoots, 2048u, s_SharedScopeJoin);
}

TEST(CpuTaskWaitProfile, DISABLED_DifferentScopeChain2048TwoWorkers){
    using namespace __hidden_cpu_task_wait_profile_tests;
    Deadline deadline;
    Alloc::ScratchArena scratch("tests/task/cpu/wait_profile");
    ProfileContributions(scratch, 2u, s_ContributionRoots, 2048u, s_DifferentScopeJoins);
}

TEST(CpuTaskWaitProfile, DISABLED_GlobalChain2048TwoWorkers){
    using namespace __hidden_cpu_task_wait_profile_tests;
    Deadline deadline;
    Alloc::ScratchArena scratch("tests/task/cpu/wait_profile");
    ProfileContributions(scratch, 2u, s_ContributionRoots, 2048u, s_GlobalJoin);
}

TEST(CpuTaskWaitProfile, DISABLED_SameScopeChain512FourWorkers){
    using namespace __hidden_cpu_task_wait_profile_tests;
    Deadline deadline;
    Alloc::ScratchArena scratch("tests/task/cpu/wait_profile");
    ProfileContributions(scratch, 4u, s_ContributionRoots, 512u, s_SharedScopeJoin);
}

TEST(CpuTaskWaitProfile, DISABLED_SameScopeChain2048FourWorkers){
    using namespace __hidden_cpu_task_wait_profile_tests;
    Deadline deadline;
    Alloc::ScratchArena scratch("tests/task/cpu/wait_profile");
    ProfileContributions(scratch, 4u, s_ContributionRoots, 2048u, s_SharedScopeJoin);
}

TEST(CpuTaskWaitProfile, DISABLED_DifferentScopeChain2048FourWorkers){
    using namespace __hidden_cpu_task_wait_profile_tests;
    Deadline deadline;
    Alloc::ScratchArena scratch("tests/task/cpu/wait_profile");
    ProfileContributions(scratch, 4u, s_ContributionRoots, 2048u, s_DifferentScopeJoins);
}

TEST(CpuTaskWaitProfile, DISABLED_GlobalChain2048FourWorkers){
    using namespace __hidden_cpu_task_wait_profile_tests;
    Deadline deadline;
    Alloc::ScratchArena scratch("tests/task/cpu/wait_profile");
    ProfileContributions(scratch, 4u, s_ContributionRoots, 2048u, s_GlobalJoin);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

