// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/cpu/scheduler.h>

#include <global/terminal_entry.h>
#include <global/termination.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_range_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_CompletionTimeoutMS = 10000u;
constexpr u32 s_PollIntervalMS = 1u;
using namespace NWB::Core;

[[nodiscard]] CpuTaskSchedulerConfig OneWorker()noexcept{
    CpuTaskSchedulerConfig config;
    config.workerCount = 1u;
    config.heterogeneous = false;
    return config;
}

void PumpUntil(CpuTaskScheduler& scheduler, const Atomic<bool>& done){
    const Timer start = TimerNow();
    while(!done.load(MemoryOrder::acquire)){
        if(DurationInMS<u64>(TimerNow(), start) > __hidden_cpu_task_range_tests::s_CompletionTimeoutMS)
            TerminateInvariant();
        scheduler.pumpMainThread();
        SleepMS(__hidden_cpu_task_range_tests::s_PollIntervalMS);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskRangeTests, SingleChunkJoinsAllSubmittedDescendants){
    using namespace __hidden_cpu_task_range_tests;
    CpuTaskScheduler scheduler(0u);
    u32 callbacks = 0u;
    EXPECT_TRUE((scheduler.parallelFor(0u, 1u, 1u, [&](usize){
        ++callbacks;
        const auto cpuTaskAdmission = scheduler.submit([&](){
            ++callbacks;
            const auto secondCpuTaskAdmission = scheduler.submit([&]()noexcept{ ++callbacks; });
            ASSERT_TRUE(secondCpuTaskAdmission);
            if(secondCpuTaskAdmission)
                ASSERT_TRUE(secondCpuTaskAdmission->valid());
        }
        );
        ASSERT_TRUE(cpuTaskAdmission);
        if(cpuTaskAdmission)
            ASSERT_TRUE(cpuTaskAdmission->valid());
    }
    )));
    EXPECT_EQ(callbacks, 3u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}

TEST(CpuTaskRangeTests, SingleChunkKeepsItsCallingWorkersIdentity){
    using namespace __hidden_cpu_task_range_tests;
    CpuTaskScheduler scheduler(OneWorker());
    const auto cpuTaskAdmission = scheduler.submit([&](){
        const usize worker = scheduler.currentWorkerIndex();
        const auto affinity = scheduler.currentWorkerAffinity();
        EXPECT_NE(worker, 0u);
        EXPECT_TRUE((scheduler.parallelFor(0u, 1u, 1u, [&](usize){
            EXPECT_EQ(scheduler.currentWorkerIndex(), worker);
            EXPECT_EQ(scheduler.currentWorkerAffinity(), affinity);
        }
        )));
    }
    );
    ASSERT_TRUE(cpuTaskAdmission);
    const auto task = *cpuTaskAdmission;
    ASSERT_TRUE(task.valid());
    scheduler.wait(task);
}

TEST(CpuTaskRangeTests, ExternalSingleChunkUsesAWorkerInsteadOfTheProducer){
    using namespace __hidden_cpu_task_range_tests;
    CpuTaskScheduler scheduler(OneWorker());
    Atomic<bool> done{ false };
    usize worker = 0u;
    Thread producer([&](){
        EXPECT_TRUE((scheduler.parallelFor(0u, 1u, 1u, [&](usize)noexcept{ worker = scheduler.currentWorkerIndex(); })));
        done.store(true, MemoryOrder::release);
    }
    );
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
        EXPECT_TRUE((scheduler.parallelFor(0u, 1u, 1u, [&](usize){
            ++callbacks;
            EXPECT_EQ(QueryCurrentThreadId(), owner);
            EXPECT_EQ(scheduler.currentWorkerIndex(), 0u);
        }, { .target = CpuTaskTarget::MainThread }
        )));
        done.store(true, MemoryOrder::release);
    }
    );
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
    const auto rejected = scheduler.parallelFor(0u, 128u, 1u, [&](usize)noexcept{ ++callbacks; }, options);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error(), CpuTaskAdmissionError::InvalidOptions);
    EXPECT_EQ(callbacks, 0u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskRangeTests, InlineFailureRetiresSubmittedDescendantsBeforeTerminalHandling){
    using namespace __hidden_cpu_task_range_tests;
    struct Failure{ int code; };
    struct Retirement{
        u32* count;


    public:
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
    const int result = ::InvokeTerminalEntry<Failure>([&]()->int{
        EXPECT_TRUE((scheduler.parallelFor(0u, 1u, 1u, [&](usize){
            const auto cpuTaskAdmission = scheduler.submit([&, capture = Retirement(retirements)](){
                EXPECT_TRUE(capture.count != nullptr);
                ++callbacks;
            }
            );
            EXPECT_TRUE(cpuTaskAdmission);
            if(cpuTaskAdmission)
                EXPECT_TRUE(cpuTaskAdmission->valid());
            throw Failure{ 73 };
        }
        )));
        return 0;
    }, [&](const Failure& failure){
        EXPECT_EQ(retirements, 1u);
        EXPECT_EQ(callbacks, 0u);
        EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
        return failure.code;
    }, []()noexcept{ return -1; }
    );
    EXPECT_EQ(result, 73);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

