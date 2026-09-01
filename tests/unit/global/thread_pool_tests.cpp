// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/exception.h>

#include <core/alloc/thread.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_thread_pool_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_ParallelCallerException = 0xE002u;
inline constexpr u32 s_ParallelWorkerException = 0xE003u;
inline constexpr u32 s_BatchBuilderException = 0xE004u;
inline constexpr u32 s_TaskConstructionException = 0xE005u;
inline constexpr u32 s_NestedParallelException = 0xE006u;
inline constexpr u32 s_ZeroWorkerTaskException = 0xE007u;
inline constexpr u32 s_QueuedTaskException = 0xE008u;
inline constexpr u32 s_UnrelatedUnwindException = 0xE009u;


struct ThrowingMoveTask{
    ThrowingMoveTask() = default;
    ThrowingMoveTask(ThrowingMoveTask&&){ throw s_TaskConstructionException; }


    void operator()()const noexcept{}
};

struct ThrowingCopyTask{
    ThrowingCopyTask() = default;
    ThrowingCopyTask(const ThrowingCopyTask&){ throw s_TaskConstructionException; }
    ThrowingCopyTask(ThrowingCopyTask&&)noexcept = default;


    void operator()()const noexcept{}
};

struct MoveOnlyTask{
    MoveOnlyTask() = default;
    MoveOnlyTask(const MoveOnlyTask&) = delete;
    MoveOnlyTask(MoveOnlyTask&&)noexcept = default;


    void operator()()const noexcept{}
};

struct LvalueOnlyTask{
    u32* invocationCount;


    explicit LvalueOnlyTask(u32& count)noexcept
        : invocationCount(&count)
    {}


    void operator()()& noexcept{ ++*invocationCount; }
    void operator()()&& = delete;
};

struct NonCallableTask{};

struct TaskLifetimeProbe{
    Atomic<u32>* destructionCount;


    explicit TaskLifetimeProbe(Atomic<u32>& count)noexcept
        : destructionCount(&count)
    {}
    TaskLifetimeProbe(const TaskLifetimeProbe&) = delete;
    TaskLifetimeProbe(TaskLifetimeProbe&& rhs)noexcept
        : destructionCount(rhs.destructionCount)
    {
        rhs.destructionCount = nullptr;
    }
    ~TaskLifetimeProbe()noexcept{
        if(!destructionCount)
            return;

        destructionCount->fetch_add(1u, MemoryOrder::release);
        destructionCount->notify_all();
    }
};


static_assert(!IsConstructible_V<InplaceFunction<128u>, ThrowingMoveTask>);
static_assert(IsConstructible_V<InplaceFunction<128u>, ThrowingCopyTask&>);
static_assert(!IsConstructible_V<InplaceFunction<128u>, MoveOnlyTask&>);
static_assert(IsConstructible_V<InplaceFunction<128u>, MoveOnlyTask>);
static_assert(IsConstructible_V<InplaceFunction<128u>, LvalueOnlyTask>);
static_assert(!IsConstructible_V<InplaceFunction<128u>, NonCallableTask>);
static_assert(IsNothrowDestructible_V<NWB::Core::Alloc::ThreadPool>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ThreadPoolTests, ParallelForCallerExceptionDrainsWorkersAndRestoresDispatchState){
    NWB::Core::Alloc::ThreadPool threadPool(2u, CpuAffinity::Any);
    AtomicFlag callerEntered;

    bool exceptionObserved = false;
    try{
        threadPool.parallelFor(0u, 64u, [&threadPool, &callerEntered](const usize){
            if(threadPool.currentWorkerIndex() == 0u){
                callerEntered.test_and_set(MemoryOrder::release);
                callerEntered.notify_all();
                throw __hidden_thread_pool_tests::s_ParallelCallerException;
            }
            while(!callerEntered.test(MemoryOrder::acquire))
                callerEntered.wait(false, MemoryOrder::acquire);
        });
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_ParallelCallerException;
    }
    EXPECT_TRUE(exceptionObserved);

    Atomic<u32> completedIterationCount{ 0u };
    EXPECT_NO_THROW(threadPool.parallelFor(0u, 64u, [&completedIterationCount](const usize){
        completedIterationCount.fetch_add(1u, MemoryOrder::relaxed);
    }));
    EXPECT_EQ(completedIterationCount.load(MemoryOrder::relaxed), 64u);
}


TEST(ThreadPoolTests, ParallelForWorkerExceptionPropagatesAfterEveryChunkResolves){
    NWB::Core::Alloc::ThreadPool threadPool(2u, CpuAffinity::Any);
    AtomicFlag workerEntered;

    bool exceptionObserved = false;
    try{
        threadPool.parallelFor(0u, 64u, [&threadPool, &workerEntered](const usize){
            if(threadPool.currentWorkerIndex() == 0u){
                while(!workerEntered.test(MemoryOrder::acquire))
                    workerEntered.wait(false, MemoryOrder::acquire);
                return;
            }
            if(workerEntered.test_and_set(MemoryOrder::acq_rel))
                return;
            workerEntered.notify_all();
            throw __hidden_thread_pool_tests::s_ParallelWorkerException;
        });
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_ParallelWorkerException;
    }
    EXPECT_TRUE(exceptionObserved);

    Atomic<u32> completedIterationCount{ 0u };
    EXPECT_NO_THROW(threadPool.parallelFor(0u, 64u, [&completedIterationCount](const usize){
        completedIterationCount.fetch_add(1u, MemoryOrder::relaxed);
    }));
    EXPECT_EQ(completedIterationCount.load(MemoryOrder::relaxed), 64u);
}


TEST(ThreadPoolTests, BatchBuilderExceptionLeavesTheQueueUnchanged){
    NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
    Atomic<u32> completedTaskCount{ 0u };

    bool exceptionObserved = false;
    try{
        threadPool.enqueueBatch(4u, [&completedTaskCount](const usize taskIndex){
            if(taskIndex == 2u)
                throw __hidden_thread_pool_tests::s_BatchBuilderException;
            return [&completedTaskCount](){ completedTaskCount.fetch_add(1u, MemoryOrder::relaxed); };
        });
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_BatchBuilderException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_NO_THROW(threadPool.wait());
    EXPECT_EQ(completedTaskCount.load(MemoryOrder::relaxed), 0u);
}


TEST(ThreadPoolTests, BatchBuilderCanReenterThePoolWithoutLockingTheQueueRecursively){
    NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
    Atomic<u32> completedTaskCount{ 0u };

    EXPECT_NO_THROW(threadPool.enqueueBatch(3u, [&threadPool, &completedTaskCount](const usize taskIndex){
        if(taskIndex == 0u){
            threadPool.enqueue([&completedTaskCount](){ completedTaskCount.fetch_add(1u, MemoryOrder::relaxed); });
            threadPool.wait();
        }
        return [&completedTaskCount](){ completedTaskCount.fetch_add(1u, MemoryOrder::relaxed); };
    }));
    EXPECT_NO_THROW(threadPool.wait());
    EXPECT_EQ(completedTaskCount.load(MemoryOrder::relaxed), 4u);
}


TEST(ThreadPoolTests, ZeroWorkerBatchPreparesAtomicallyAndExecutesOnTheCaller){
    NWB::Core::Alloc::ThreadPool threadPool(0u, CpuAffinity::Any);
    u32 completedTaskCount = 0u;

    EXPECT_NO_THROW(threadPool.enqueueBatch(3u, [&completedTaskCount](const usize){
        return [&completedTaskCount](){ ++completedTaskCount; };
    }));
    EXPECT_EQ(completedTaskCount, 3u);

    bool exceptionObserved = false;
    try{
        threadPool.enqueueBatch(3u, [&completedTaskCount](const usize taskIndex){
            return [&completedTaskCount, taskIndex](){
                if(taskIndex == 1u)
                    throw __hidden_thread_pool_tests::s_ZeroWorkerTaskException;
                ++completedTaskCount;
            };
        });
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_ZeroWorkerTaskException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_EQ(completedTaskCount, 4u);
}


TEST(ThreadPoolTests, ZeroWorkerBatchBuilderExceptionRunsNoPreparedTask){
    NWB::Core::Alloc::ThreadPool threadPool(0u, CpuAffinity::Any);
    u32 completedTaskCount = 0u;

    bool exceptionObserved = false;
    try{
        threadPool.enqueueBatch(3u, [&completedTaskCount](const usize taskIndex){
            if(taskIndex == 1u)
                throw __hidden_thread_pool_tests::s_BatchBuilderException;
            return [&completedTaskCount](){ ++completedTaskCount; };
        });
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_BatchBuilderException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_EQ(completedTaskCount, 0u);
}


TEST(ThreadPoolTests, TaskConstructionExceptionDoesNotCreatePendingWork){
    NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
    __hidden_thread_pool_tests::ThrowingCopyTask task;

    bool exceptionObserved = false;
    try{
        threadPool.enqueue(task);
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_TaskConstructionException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_NO_THROW(threadPool.wait());
}


TEST(ThreadPoolTests, ZeroWorkerEnqueuePreservesQueuedCallableConstructionAndInvocationSemantics){
    NWB::Core::Alloc::ThreadPool threadPool(0u, CpuAffinity::Any);
    __hidden_thread_pool_tests::ThrowingCopyTask throwingCopyTask;

    bool exceptionObserved = false;
    try{
        threadPool.enqueue(throwingCopyTask);
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_TaskConstructionException;
    }
    EXPECT_TRUE(exceptionObserved);

    u32 invocationCount = 0u;
    EXPECT_NO_THROW(threadPool.enqueue(__hidden_thread_pool_tests::LvalueOnlyTask(invocationCount)));
    EXPECT_EQ(invocationCount, 1u);
}


TEST(ThreadPoolTests, QueuedTaskExceptionCancelsQueuedTasksAndRejectsFutureAdmission){
    constexpr usize canceledTaskCount = 8u;

    NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
    AtomicFlag throwingTaskEntered;
    AtomicFlag releaseThrowingTask;
    Atomic<u32> canceledCaptureCount{ 0u };
    Atomic<u32> followerInvocationCount{ 0u };

    threadPool.enqueue([&throwingTaskEntered, &releaseThrowingTask](){
        throwingTaskEntered.test_and_set(MemoryOrder::release);
        throwingTaskEntered.notify_all();
        while(!releaseThrowingTask.test(MemoryOrder::acquire))
            releaseThrowingTask.wait(false, MemoryOrder::acquire);
        throw __hidden_thread_pool_tests::s_QueuedTaskException;
    });
    while(!throwingTaskEntered.test(MemoryOrder::acquire))
        throwingTaskEntered.wait(false, MemoryOrder::acquire);

    for(usize i = 0u; i < canceledTaskCount; ++i){
        threadPool.enqueue([
            lifetimeProbe = __hidden_thread_pool_tests::TaskLifetimeProbe(canceledCaptureCount),
            &followerInvocationCount
        ]() mutable{
            followerInvocationCount.fetch_add(1u, MemoryOrder::relaxed);
        });
    }

    releaseThrowingTask.test_and_set(MemoryOrder::release);
    releaseThrowingTask.notify_all();

    bool exceptionObserved = false;
    try{
        threadPool.wait();
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_QueuedTaskException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_EQ(followerInvocationCount.load(MemoryOrder::relaxed), 0u);
    EXPECT_EQ(canceledCaptureCount.load(MemoryOrder::acquire), static_cast<u32>(canceledTaskCount));

    Atomic<u32> rejectedTaskInvocationCount{ 0u };
    bool enqueueExceptionObserved = false;
    try{
        threadPool.enqueue([&rejectedTaskInvocationCount](){
            rejectedTaskInvocationCount.fetch_add(1u, MemoryOrder::relaxed);
        });
    }
    catch(const u32 exception){
        enqueueExceptionObserved = exception == __hidden_thread_pool_tests::s_QueuedTaskException;
    }
    EXPECT_TRUE(enqueueExceptionObserved);
    EXPECT_EQ(rejectedTaskInvocationCount.load(MemoryOrder::relaxed), 0u);

    Atomic<u32> rejectedBuilderInvocationCount{ 0u };
    bool batchExceptionObserved = false;
    try{
        threadPool.enqueueBatch(4u, [&rejectedBuilderInvocationCount](const usize){
            rejectedBuilderInvocationCount.fetch_add(1u, MemoryOrder::relaxed);
            return []()noexcept{};
        });
    }
    catch(const u32 exception){
        batchExceptionObserved = exception == __hidden_thread_pool_tests::s_QueuedTaskException;
    }
    EXPECT_TRUE(batchExceptionObserved);
    EXPECT_EQ(rejectedBuilderInvocationCount.load(MemoryOrder::relaxed), 0u);
}


TEST(ThreadPoolTests, QueuedTaskFailureWaitsForEveryAlreadyActiveWorker){
    NWB::Core::Alloc::ThreadPool threadPool(2u, CpuAffinity::Any);
    Latch activeTasksReady(2);
    AtomicFlag releaseThrowingTask;
    AtomicFlag releaseSuccessfulTask;
    AtomicFlag waiterEntered;
    AtomicFlag waiterFinished;
    Atomic<u32> canceledCaptureCount{ 0u };
    Atomic<u32> followerInvocationCount{ 0u };
    Atomic<u32> observedException{ 0u };

    threadPool.enqueue([&activeTasksReady, &releaseThrowingTask](){
        activeTasksReady.count_down();
        activeTasksReady.wait();
        while(!releaseThrowingTask.test(MemoryOrder::acquire))
            releaseThrowingTask.wait(false, MemoryOrder::acquire);
        throw __hidden_thread_pool_tests::s_QueuedTaskException;
    });
    threadPool.enqueue([&activeTasksReady, &releaseSuccessfulTask](){
        activeTasksReady.count_down();
        activeTasksReady.wait();
        while(!releaseSuccessfulTask.test(MemoryOrder::acquire))
            releaseSuccessfulTask.wait(false, MemoryOrder::acquire);
    });
    activeTasksReady.wait();

    threadPool.enqueue([
        lifetimeProbe = __hidden_thread_pool_tests::TaskLifetimeProbe(canceledCaptureCount),
        &followerInvocationCount
    ]() mutable{
        followerInvocationCount.fetch_add(1u, MemoryOrder::relaxed);
    });

    JoiningThread waiter([&](){
        waiterEntered.test_and_set(MemoryOrder::release);
        waiterEntered.notify_all();
        try{
            threadPool.wait();
        }
        catch(const u32 exception){
            observedException.store(exception, MemoryOrder::release);
        }
        waiterFinished.test_and_set(MemoryOrder::release);
        waiterFinished.notify_all();
    });
    while(!waiterEntered.test(MemoryOrder::acquire))
        waiterEntered.wait(false, MemoryOrder::acquire);

    releaseThrowingTask.test_and_set(MemoryOrder::release);
    releaseThrowingTask.notify_all();

    u32 currentCaptureCount = canceledCaptureCount.load(MemoryOrder::acquire);
    while(currentCaptureCount == 0u){
        canceledCaptureCount.wait(currentCaptureCount, MemoryOrder::relaxed);
        currentCaptureCount = canceledCaptureCount.load(MemoryOrder::acquire);
    }
    EXPECT_FALSE(waiterFinished.test(MemoryOrder::acquire));

    releaseSuccessfulTask.test_and_set(MemoryOrder::release);
    releaseSuccessfulTask.notify_all();
    waiter.join();

    EXPECT_TRUE(waiterFinished.test(MemoryOrder::acquire));
    EXPECT_EQ(observedException.load(MemoryOrder::acquire), __hidden_thread_pool_tests::s_QueuedTaskException);
    EXPECT_EQ(followerInvocationCount.load(MemoryOrder::relaxed), 0u);
    EXPECT_EQ(canceledCaptureCount.load(MemoryOrder::acquire), 1u);
}


TEST(ThreadPoolTests, NestedSerialParallelForExceptionPropagatesThroughTheOuterDispatch){
    NWB::Core::Alloc::ThreadPool threadPool(2u, CpuAffinity::Any);

    bool exceptionObserved = false;
    try{
        threadPool.parallelFor(0u, 64u, [&threadPool](const usize){
            threadPool.parallelFor(0u, 2u, [](const usize){
                throw __hidden_thread_pool_tests::s_NestedParallelException;
            });
        });
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_NestedParallelException;
    }
    EXPECT_TRUE(exceptionObserved);

    Atomic<u32> completedIterationCount{ 0u };
    EXPECT_NO_THROW(threadPool.parallelFor(0u, 64u, [&completedIterationCount](const usize){
        completedIterationCount.fetch_add(1u, MemoryOrder::relaxed);
    }));
    EXPECT_EQ(completedIterationCount.load(MemoryOrder::relaxed), 64u);
}


TEST(ThreadPoolTests, ParallelForDescriptorRemainsAliveUntilEveryWorkerQuiesces){
    NWB::Core::Alloc::ThreadPool threadPool(2u, CpuAffinity::Any);
    Atomic<u32> completedIterationCount{ 0u };

    for(usize dispatchIndex = 0u; dispatchIndex < 256u; ++dispatchIndex){
        EXPECT_NO_THROW(threadPool.parallelFor(0u, 2u, [&completedIterationCount](const usize){
            completedIterationCount.fetch_add(1u, MemoryOrder::relaxed);
        }));
    }

    EXPECT_EQ(completedIterationCount.load(MemoryOrder::relaxed), 512u);
}


TEST(ThreadPoolTests, NestedParallelForCycleSerializesAgainstEveryAncestorPool){
    NWB::Core::Alloc::ThreadPool poolA(1u, CpuAffinity::Any);
    NWB::Core::Alloc::ThreadPool poolB(1u, CpuAffinity::Any);
    Atomic<u32> completedIterationCount{ 0u };

    EXPECT_NO_THROW(poolA.parallelFor(0u, 2u, [&poolA, &poolB, &completedIterationCount](const usize){
        poolB.parallelFor(0u, 2u, [&poolA, &completedIterationCount](const usize){
            poolA.parallelFor(0u, 2u, [&completedIterationCount](const usize){
                completedIterationCount.fetch_add(1u, MemoryOrder::relaxed);
            });
        });
    }));
    EXPECT_EQ(completedIterationCount.load(MemoryOrder::relaxed), 8u);
}


TEST(ThreadPoolTests, OppositeNestedPoolAcquisitionsCannotInvertTheParallelDispatchLockOrder){
    NWB::Core::Alloc::ThreadPool lowerDomainPool(1u, CpuAffinity::Any);
    NWB::Core::Alloc::ThreadPool higherDomainPool(1u, CpuAffinity::Any);
    Latch outerDispatchesReady(2);
    AtomicFlag lowerDomainDispatchEntered;
    AtomicFlag higherDomainDispatchEntered;
    Atomic<u32> completedIterationCount{ 0u };

    JoiningThread lowerDomainThread([&](){
        lowerDomainPool.parallelFor(0u, 2u, [&](const usize){
            if(lowerDomainDispatchEntered.test_and_set(MemoryOrder::acq_rel))
                return;
            outerDispatchesReady.count_down();
            outerDispatchesReady.wait();
            higherDomainPool.parallelFor(0u, 2u, [&](const usize){
                completedIterationCount.fetch_add(1u, MemoryOrder::relaxed);
            });
        });
    });
    JoiningThread higherDomainThread([&](){
        higherDomainPool.parallelFor(0u, 2u, [&](const usize){
            if(higherDomainDispatchEntered.test_and_set(MemoryOrder::acq_rel))
                return;
            outerDispatchesReady.count_down();
            outerDispatchesReady.wait();
            lowerDomainPool.parallelFor(0u, 2u, [&](const usize){
                completedIterationCount.fetch_add(1u, MemoryOrder::relaxed);
            });
        });
    });

    lowerDomainThread.join();
    higherDomainThread.join();
    EXPECT_EQ(completedIterationCount.load(MemoryOrder::relaxed), 4u);
}


TEST(ThreadPoolTests, FinishPropagatesTheExactTaskException){
    NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
    threadPool.enqueue([](){ throw __hidden_thread_pool_tests::s_QueuedTaskException; });

    bool exceptionObserved = false;
    try{
        threadPool.finish();
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_QueuedTaskException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_NO_THROW(threadPool.finish());
    EXPECT_NO_THROW(threadPool.wait());
}


TEST(ThreadPoolTests, TaskExceptionDoesNotReplaceActiveUnwind){
    bool exceptionObserved = false;
    try{
        NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
        threadPool.enqueue([](){ throw __hidden_thread_pool_tests::s_QueuedTaskException; });
        throw __hidden_thread_pool_tests::s_UnrelatedUnwindException;
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_UnrelatedUnwindException;
    }
    EXPECT_TRUE(exceptionObserved);
}


TEST(ThreadPoolTests, WorkerSelfWaitFailsTheDomainWithoutDeadlocking){
    NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
    threadPool.enqueue([&threadPool](){ threadPool.wait(); });

    bool exceptionObserved = false;
    try{
        threadPool.wait();
    }
    catch(const RuntimeException&){
        exceptionObserved = true;
    }
    EXPECT_TRUE(exceptionObserved);
}


TEST(ThreadPoolTests, SamePoolWorkerAdmissionDoesNotObserveTheDomainFailure){
    Latch workersReady(2u);
    AtomicFlag releaseThrowingTask;
    Atomic<u32> canceledCaptureCount{ 0u };

    bool exceptionObserved = false;
    try{
        NWB::Core::Alloc::ThreadPool threadPool(2u, CpuAffinity::Any);
        threadPool.enqueue([&workersReady, &releaseThrowingTask](){
            workersReady.count_down();
            workersReady.wait();
            while(!releaseThrowingTask.test(MemoryOrder::acquire))
                releaseThrowingTask.wait(false, MemoryOrder::acquire);
            throw __hidden_thread_pool_tests::s_QueuedTaskException;
        });
        threadPool.enqueue([&threadPool, &workersReady, &canceledCaptureCount](){
            workersReady.count_down();
            workersReady.wait();

            u32 currentCaptureCount = canceledCaptureCount.load(MemoryOrder::acquire);
            while(currentCaptureCount == 0u){
                canceledCaptureCount.wait(currentCaptureCount, MemoryOrder::relaxed);
                currentCaptureCount = canceledCaptureCount.load(MemoryOrder::acquire);
            }
            threadPool.enqueue([]()noexcept{});
        });
        workersReady.wait();

        threadPool.enqueue([
            lifetimeProbe = __hidden_thread_pool_tests::TaskLifetimeProbe(canceledCaptureCount)
        ]() mutable{});
        releaseThrowingTask.test_and_set(MemoryOrder::release);
        releaseThrowingTask.notify_all();
        threadPool.finish();
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_QueuedTaskException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_EQ(canceledCaptureCount.load(MemoryOrder::acquire), 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

