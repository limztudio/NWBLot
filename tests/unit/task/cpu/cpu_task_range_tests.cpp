// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/cpu/scheduler.h>
#include <core/common/terminal_entry.h>

#include <global/termination.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_range_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

[[nodiscard]] CpuTaskSchedulerConfig OneWorker(){
    CpuTaskSchedulerConfig config;
    config.workerCount = 1u;
    config.heterogeneous = false;
    return config;
}

void PumpUntil(CpuTaskScheduler& scheduler, const Atomic<bool>& done){
    const Timer start = TimerNow();
    while(!done.load(MemoryOrder::acquire)){
        if(DurationInMS<u64>(TimerNow(), start) > 10000u)
            TerminateInvariant();
        scheduler.pumpMainThread();
        SleepMS(1u);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskRangeTests, SingleChunkJoinsAllSubmittedDescendants){
    using namespace __hidden_cpu_task_range_tests;
    CpuTaskScheduler scheduler(0u);
    u32 callbacks = 0u;
    scheduler.parallelFor(0u, 1u, 1u, [&](usize){
        ++callbacks;
        ASSERT_TRUE(scheduler.submit([&](){
            ++callbacks;
            ASSERT_TRUE(scheduler.submit([&](){ ++callbacks; }).valid());
        }).valid());
    });
    EXPECT_EQ(callbacks, 3u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}

TEST(CpuTaskRangeTests, SingleChunkKeepsItsCallingWorkersIdentity){
    using namespace __hidden_cpu_task_range_tests;
    CpuTaskScheduler scheduler(OneWorker());
    const auto task = scheduler.submit([&](){
        const usize worker = scheduler.currentWorkerIndex();
        const auto affinity = scheduler.currentWorkerAffinity();
        EXPECT_NE(worker, 0u);
        scheduler.parallelFor(0u, 1u, 1u, [&](usize){
            EXPECT_EQ(scheduler.currentWorkerIndex(), worker);
            EXPECT_EQ(scheduler.currentWorkerAffinity(), affinity);
        });
    });
    ASSERT_TRUE(task.valid());
    scheduler.wait(task);
}

TEST(CpuTaskRangeTests, ExternalSingleChunkUsesAWorkerInsteadOfTheProducer){
    using namespace __hidden_cpu_task_range_tests;
    CpuTaskScheduler scheduler(OneWorker());
    Atomic<bool> done{ false };
    usize worker = 0u;
    Thread producer([&](){
        scheduler.parallelFor(0u, 1u, 1u, [&](usize){ worker = scheduler.currentWorkerIndex(); });
        done.store(true, MemoryOrder::release);
    });
    PumpUntil(scheduler, done);
    producer.join();
    EXPECT_EQ(worker, 1u);
}

TEST(CpuTaskRangeTests, ExternalMainThreadRangeWaitsForTheOwner){
    using namespace __hidden_cpu_task_range_tests;
    CpuTaskScheduler scheduler(OneWorker());
    Atomic<bool> done{ false };
    const ThreadId owner = QueryCurrentThreadId();
    u32 callbacks = 0u;
    Thread producer([&](){
        scheduler.parallelFor(0u, 1u, 1u, [&](usize){
            ++callbacks;
            EXPECT_EQ(QueryCurrentThreadId(), owner);
            EXPECT_EQ(scheduler.currentWorkerIndex(), 0u);
        }, { .target = CpuTaskTarget::MainThread });
        done.store(true, MemoryOrder::release);
    });
    PumpUntil(scheduler, done);
    producer.join();
    EXPECT_EQ(callbacks, 1u);
}

TEST(CpuTaskRangeTests, InvalidBatchOptionsPublishNoTasks){
    using namespace __hidden_cpu_task_range_tests;
    CpuTaskScheduler scheduler(0u);
    u32 callbacks = 0u;
    CpuTaskOptions options;
    options.cost = static_cast<CpuTaskCost::Enum>(255u);
    EXPECT_THROW(scheduler.parallelFor(0u, 128u, 1u, [&](usize){ ++callbacks; }, options), RuntimeException);
    EXPECT_EQ(callbacks, 0u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskRangeTests, InlineFailureRetiresSubmittedDescendantsBeforeTerminalHandling){
    using namespace __hidden_cpu_task_range_tests;
    struct Failure{ int code; };
    struct Retirement{
        u32* count;

        explicit Retirement(u32& counter)noexcept : count(&counter){}
        Retirement(const Retirement&) = delete;
        Retirement(Retirement&& other)noexcept : count(other.count){ other.count = nullptr; }
        ~Retirement()noexcept{
            if(count)
                ++*count;
        }
    };
    u32 retirements = 0u;
    u32 callbacks = 0u;
    CpuTaskScheduler scheduler(0u);
    const int result = Common::InvokeTerminalEntry<Failure>([&]()->int{
        scheduler.parallelFor(0u, 1u, 1u, [&](usize){
            EXPECT_TRUE(scheduler.submit([&, capture = Retirement(retirements)](){
                static_cast<void>(capture);
                ++callbacks;
            }).valid());
            throw Failure{ 73 };
        });
        return 0;
    }, [&](const Failure& failure){
        EXPECT_EQ(retirements, 1u);
        EXPECT_EQ(callbacks, 0u);
        EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
        return failure.code;
    }, [](){ return -1; });
    EXPECT_EQ(result, 73);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

