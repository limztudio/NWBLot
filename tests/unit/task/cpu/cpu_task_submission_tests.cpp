// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/cpu/scheduler.h>
#include <global/terminal_entry.h>
#include <global/platform.h>
#include <global/termination.h>
#include <global/timer.h>

#include <gtest/gtest.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX)
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_submission_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

inline constexpr u32 s_ConstructionException = 0xE101u;
inline constexpr u32 s_WorkerException = 0xE102u;
inline constexpr u32 s_TestTimeoutMS = 15000u;


struct ThrowingMoveTask{
    ThrowingMoveTask() = default;
    ThrowingMoveTask(ThrowingMoveTask&&){ throw s_ConstructionException; }


    void operator()()const noexcept{}
};

struct ThrowingCopyTask{
    ThrowingCopyTask() = default;
    ThrowingCopyTask(const ThrowingCopyTask&){ throw s_ConstructionException; }
    ThrowingCopyTask(ThrowingCopyTask&&)noexcept = default;


    void operator()()const noexcept{}
};


struct NonCallableTask{};


static_assert(!IsConstructible_V<InplaceFunction<128u>, ThrowingMoveTask>);
static_assert(!IsConstructible_V<InplaceFunction<128u>, NonCallableTask>);


[[noreturn]] void ExitTestProcess(u32 code)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    ExitProcess(code);
#elif defined(NWB_PLATFORM_LINUX)
    ::_exit(static_cast<int>(code));
#else
    static_cast<void>(code);
    TerminateInvariant();
#endif
}


class DeadlineGuard final{
public:
    DeadlineGuard()
        : m_watchdog([](const StopToken& stop){
            const Timer begin = TimerNow();
            while(!stop.stop_requested()){
                // A watchdog timeout must fail the death test instead of passing as an unexpected worker failure.
                if(DurationInMS<u64>(TimerNow(), begin) >= s_TestTimeoutMS)
                    ExitTestProcess(0u);
                SleepMS(1u);
            }
        })
    {}
    ~DeadlineGuard() = default;


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
        EXPECT_FALSE(task.valid());
        return -1;
    }, handleError, []()noexcept{ return -2; });
    EXPECT_EQ(schedulerResult, static_cast<int>(s_ConstructionException));
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);

    const int scopeResult = InvokeTerminalEntry<u32>([&]()->int{
        const auto task = scope.submit(callable);
        EXPECT_FALSE(task.valid());
        return -1;
    }, handleError, []()noexcept{ return -2; });
    EXPECT_EQ(scopeResult, static_cast<int>(s_ConstructionException));
    EXPECT_EQ(handled, s_ExpectedDualCount);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
    EXPECT_EQ(scheduler.statistics().completedTasks, 0u);
    EXPECT_EQ(scheduler.statistics().canceledTasks, 0u);
    scope.wait();
    scheduler.wait();
}


TEST(CpuTaskSubmissionTests, WorkerCallbackFailureTerminatesWithoutDeferredCallerDelivery){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_submission_tests;
        DeadlineGuard deadline;
        const int result = InvokeTerminalEntry<u32>([]()->int{
            CpuTaskScheduler scheduler(1u);
            const auto task = scheduler.submit([&](){
                if(scheduler.currentWorkerIndex() == 0u)
                    ExitTestProcess(0u);
                throw s_WorkerException;
            });
            if(!task.valid())
                return 0;
            scheduler.wait(task);
            return 0;
        }, [](const u32){ return 0; }, []()noexcept{ return 0; });
        ExitTestProcess(static_cast<u32>(result));
    }, "");
}


TEST(CpuTaskSubmissionTests, ParallelRangeWorkerFailureTerminatesWithoutDeferredCallerDelivery){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_submission_tests;
        DeadlineGuard deadline;
        const int result = InvokeTerminalEntry<u32>([]()->int{
            CpuTaskScheduler scheduler(1u);
            AtomicFlag workerEntered;
            scheduler.parallelFor(0u, 64u, [&](const usize){
                if(scheduler.currentWorkerIndex() == 0u){
                    while(!workerEntered.test(MemoryOrder::acquire))
                        workerEntered.wait(false, MemoryOrder::acquire);
                    return;
                }
                workerEntered.testAndSet(MemoryOrder::release);
                workerEntered.notifyAll();
                throw s_WorkerException;
            });
            return 0;
        }, [](const u32){ return 0; }, []()noexcept{ return 0; });
        ExitTestProcess(static_cast<u32>(result));
    }, "");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

