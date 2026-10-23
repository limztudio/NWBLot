// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "process_exit.h"

#include <core/task/cpu/scheduler.h>

#include <global/terminal_entry.h>
#include <global/platform.h>
#include <global/termination.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_submission_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_PollIntervalMS = 1u;
constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;
using CpuTaskTestSupport::ExitTestProcess;

inline constexpr u32 s_ConstructionException = 0xE101u;
inline constexpr u32 s_WorkerException = 0xE102u;
inline constexpr u32 s_TestTimeoutMS = 15000u;


struct ThrowingMoveTask{
    ThrowingMoveTask()noexcept = default;
    ThrowingMoveTask(ThrowingMoveTask&&){ throw s_ConstructionException; }


public:
    void operator()()const noexcept{}
};

struct ThrowingCopyTask{
    ThrowingCopyTask()noexcept = default;
    ThrowingCopyTask(const ThrowingCopyTask&){ throw s_ConstructionException; }
    ThrowingCopyTask(ThrowingCopyTask&&)noexcept = default;


public:
    void operator()()const noexcept{}
};


struct NonCallableTask{};


static_assert(!IsConstructible_V<InplaceFunction<128u>, ThrowingMoveTask>);
static_assert(!IsConstructible_V<InplaceFunction<128u>, NonCallableTask>);


class DeadlineGuard final{
public:
    DeadlineGuard()
        : m_watchdog([](const StopToken& stop)noexcept{
            const Timer begin = TimerNow();
            while(!stop.stop_requested()){
                // A watchdog timeout must fail the death test instead of passing as an unexpected worker failure.
                if(DurationInMS<u64>(TimerNow(), begin) >= s_TestTimeoutMS)
                    ExitTestProcess(0u);
                SleepMS(__hidden_cpu_task_submission_tests::s_PollIntervalMS);
            }
        }
        )
    {}
    ~DeadlineGuard()noexcept = default;


private:
    JoiningThread m_watchdog;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskSubmissionTests, CallableConstructionFailurePublishesNoSchedulerOrScopeWork){
    using namespace __hidden_cpu_task_submission_tests;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    ThrowingCopyTask callable;
    u32 handled = 0u;
    const auto handleError = [&handled](const u32 error)noexcept{
        ++handled;
        return static_cast<int>(error);
    };
    const int schedulerResult = InvokeTerminalEntry<u32>([&]()->int{
        const auto task = scheduler.submit(callable);
        EXPECT_FALSE(task);
        return -1;
    }, handleError, []()noexcept{ return -2; }
    );
    EXPECT_EQ(schedulerResult, static_cast<int>(s_ConstructionException));
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);

    const int scopeResult = InvokeTerminalEntry<u32>([&]()->int{
        const auto task = scope.submit(callable);
        EXPECT_FALSE(task);
        return -1;
    }, handleError, []()noexcept{ return -2; }
    );
    EXPECT_EQ(scopeResult, static_cast<int>(s_ConstructionException));
    EXPECT_EQ(handled, s_ExpectedDualCount);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
    EXPECT_EQ(scheduler.statistics().completedTasks, 0u);
    EXPECT_EQ(scheduler.statistics().canceledTasks, 0u);
    scope.wait();
    scheduler.wait();
}


TEST(CpuTaskSubmissionTests, ExpectedAdmissionErrorsPreservePublicationAndCancellationBoundaries){
    using namespace __hidden_cpu_task_submission_tests;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope canceled(scheduler);
    u32 callbacks = 0u;
    const auto invalidDependencies = scheduler.submit([&]()noexcept{ ++callbacks; }, {}, nullptr, 1u);
    ASSERT_FALSE(invalidDependencies);
    EXPECT_EQ(invalidDependencies.error(), CpuTaskAdmissionError::InvalidDependencies);
    CpuTaskOptions options;
    options.target = static_cast<CpuTaskTarget::Enum>(CpuTaskTarget::MainThread + 1u);
    const auto invalidOptions = scheduler.submit([&]()noexcept{ ++callbacks; }, options);
    ASSERT_FALSE(invalidOptions);
    EXPECT_EQ(invalidOptions.error(), CpuTaskAdmissionError::InvalidOptions);
    canceled.cancel();
    const auto scopeRejected = canceled.submit([&]()noexcept{ ++callbacks; });
    ASSERT_FALSE(scopeRejected);
    EXPECT_EQ(scopeRejected.error(), CpuTaskAdmissionError::ScopeCanceled);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
    EXPECT_EQ(callbacks, 0u);
    const auto accepted = scheduler.submit([&]()noexcept{ ++callbacks; });
    ASSERT_TRUE(accepted);
    EXPECT_TRUE(accepted->valid());
    scheduler.wait(*accepted);
    EXPECT_EQ(callbacks, 1u);
    scheduler.drain();
    const auto shutdownRejected = scheduler.submit([&]()noexcept{ ++callbacks; });
    ASSERT_FALSE(shutdownRejected);
    EXPECT_EQ(shutdownRejected.error(), CpuTaskAdmissionError::Aborting);
    EXPECT_EQ(callbacks, 1u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskSubmissionTests, WorkerCallbackFailureTerminatesWithoutDeferredCallerDelivery){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_submission_tests;
        DeadlineGuard deadline;
        const int result = InvokeTerminalEntry<u32>([]()->int{
            CpuTaskScheduler scheduler(1u);
            const auto cpuTaskAdmission = scheduler.submit([&](){
                if(scheduler.currentWorkerIndex() == 0u)
                    ExitTestProcess(0u);
                throw s_WorkerException;
            }
            );
            if(!cpuTaskAdmission)
                ExitTestProcess(0u);
            const auto task = *cpuTaskAdmission;
            if(!task.valid())
                return 0;
            scheduler.wait(task);
            return 0;
        }, [](const u32)noexcept{ return 0; }, []()noexcept{ return 0; }
        );
        ExitTestProcess(static_cast<u32>(result));
    }, ""
    );
}


TEST(CpuTaskSubmissionTests, ParallelRangeWorkerFailureTerminatesWithoutDeferredCallerDelivery){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_submission_tests;
        DeadlineGuard deadline;
        const int result = InvokeTerminalEntry<u32>([]()->int{
            CpuTaskScheduler scheduler(1u);
            AtomicFlag workerEntered;
            EXPECT_TRUE((scheduler.parallelFor(0u, 64u, [&](const usize){
                if(scheduler.currentWorkerIndex() == 0u){
                    while(!workerEntered.test(MemoryOrder::acquire))
                        workerEntered.wait(false, MemoryOrder::acquire);
                    return;
                }
                workerEntered.testAndSet(MemoryOrder::release);
                workerEntered.notifyAll();
                throw s_WorkerException;
            }
            )));
            return 0;
        }, [](const u32)noexcept{ return 0; }, []()noexcept{ return 0; }
        );
        ExitTestProcess(static_cast<u32>(result));
    }, ""
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

