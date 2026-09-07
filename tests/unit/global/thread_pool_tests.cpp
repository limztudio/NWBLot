// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/scope_exit.h>

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


TEST(ThreadPoolTests, ParallelForCallerExceptionRetiresEveryActiveCallbackBeforeUnwinding){
    NWB::Core::Alloc::ThreadPool threadPool(2u, CpuAffinity::Any);
    AtomicFlag callerEntered;
    Atomic<u32> activeCallbacks{ 0u };
    Latch workersReady(2u);
    bool exceptionObserved = false;
    try{
        threadPool.parallelFor(0u, 64u, [&](const usize){
            if(threadPool.currentWorkerIndex() == 0u){
                workersReady.wait();
                callerEntered.test_and_set(MemoryOrder::release);
                callerEntered.notify_all();
                throw __hidden_thread_pool_tests::s_ParallelCallerException;
            }
            activeCallbacks.fetch_add(1u, MemoryOrder::release);
            if(!callerEntered.test(MemoryOrder::acquire)){
                workersReady.count_down();
                while(!callerEntered.test(MemoryOrder::acquire))
                    callerEntered.wait(false, MemoryOrder::acquire);
            }
            activeCallbacks.fetch_sub(1u, MemoryOrder::release);
        });
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_ParallelCallerException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_EQ(activeCallbacks.load(MemoryOrder::acquire), 0u);
}


TEST(ThreadPoolTests, ParallelForWorkerExceptionTerminatesAtTheNativeThreadBoundary){
    EXPECT_DEATH({
        NWB::Core::Alloc::ThreadPool threadPool(2u, CpuAffinity::Any);
        AtomicFlag workerEntered;
        threadPool.parallelFor(0u, 64u, [&](const usize){
            if(threadPool.currentWorkerIndex() == 0u){
                while(!workerEntered.test(MemoryOrder::acquire))
                    workerEntered.wait(false, MemoryOrder::acquire);
                return;
            }
            workerEntered.test_and_set(MemoryOrder::release);
            workerEntered.notify_all();
            throw __hidden_thread_pool_tests::s_ParallelWorkerException;
        });
    }, "");
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
    u32 invocationCount = 0u;
    EXPECT_NO_THROW(threadPool.enqueue(__hidden_thread_pool_tests::LvalueOnlyTask(invocationCount)));
    EXPECT_EQ(invocationCount, 1u);

    __hidden_thread_pool_tests::ThrowingCopyTask throwingCopyTask;

    bool exceptionObserved = false;
    try{
        threadPool.enqueue(throwingCopyTask);
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_TaskConstructionException;
    }
    EXPECT_TRUE(exceptionObserved);

}


TEST(ThreadPoolTests, QueuedTaskExceptionTerminatesWithoutDeferredDelivery){
    EXPECT_DEATH({
        NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
        threadPool.enqueue([](){ throw __hidden_thread_pool_tests::s_QueuedTaskException; });
        threadPool.drain();
    }, "");
}


TEST(ThreadPoolTests, CallerUnwindDrainsNonthrowingTasksAndTheirCaptures){
    AtomicFlag releaseTask;
    Atomic<u32> retiredCaptures{ 0u };
    bool exceptionObserved = false;
    try{
        NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
        ScopeExit releaseWork([&]()noexcept{
            releaseTask.test_and_set(MemoryOrder::release);
            releaseTask.notify_all();
        });
        threadPool.enqueue([
            &releaseTask,
            lifetime = __hidden_thread_pool_tests::TaskLifetimeProbe(retiredCaptures)
        ](){
            while(!releaseTask.test(MemoryOrder::acquire))
                releaseTask.wait(false, MemoryOrder::acquire);
        });
        throw __hidden_thread_pool_tests::s_UnrelatedUnwindException;
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_UnrelatedUnwindException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_EQ(retiredCaptures.load(MemoryOrder::acquire), 1u);
}


TEST(ThreadPoolTests, NestedCallerParallelExceptionUnwindsBothDispatchScopes){
    NWB::Core::Alloc::ThreadPool threadPool(2u, CpuAffinity::Any);
    AtomicFlag callerEntered;
    bool exceptionObserved = false;
    try{
        threadPool.parallelFor(0u, 64u, [&](const usize){
            if(threadPool.currentWorkerIndex() != 0u){
                while(!callerEntered.test(MemoryOrder::acquire))
                    callerEntered.wait(false, MemoryOrder::acquire);
                return;
            }
            callerEntered.test_and_set(MemoryOrder::release);
            callerEntered.notify_all();
            threadPool.parallelFor(0u, 2u, [](const usize){
                throw __hidden_thread_pool_tests::s_NestedParallelException;
            });
        });
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_NestedParallelException;
    }
    EXPECT_TRUE(exceptionObserved);
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


TEST(ThreadPoolTests, InlineTaskExceptionPreservesTheOriginalExceptionAndReleasesItsCapture){
    Atomic<u32> retiredCaptures{ 0u };
    bool exceptionObserved = false;
    try{
        NWB::Core::Alloc::ThreadPool threadPool(0u, CpuAffinity::Any);
        threadPool.enqueue([lifetime = __hidden_thread_pool_tests::TaskLifetimeProbe(retiredCaptures)](){
            throw __hidden_thread_pool_tests::s_ZeroWorkerTaskException;
        });
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_ZeroWorkerTaskException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_EQ(retiredCaptures.load(MemoryOrder::acquire), 1u);
}


TEST(ThreadPoolTests, WorkerExceptionRemainsTerminalDuringAnUnrelatedCallerUnwind){
    EXPECT_DEATH({
        NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
        threadPool.enqueue([](){ throw __hidden_thread_pool_tests::s_QueuedTaskException; });
        throw __hidden_thread_pool_tests::s_UnrelatedUnwindException;
    }, "");
}


TEST(ThreadPoolTests, WorkerSelfWaitTerminatesWithoutDeadlocking){
    EXPECT_DEATH({
        NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
        threadPool.enqueue([&threadPool](){ threadPool.wait(); });
        threadPool.drain();
    }, "");
}


TEST(ThreadPoolTests, BatchBuilderUnwindDestroysEveryPreparedCaptureWithoutInvocation){
    Atomic<u32> retiredCaptures{ 0u };
    Atomic<u32> invokedTasks{ 0u };
    bool exceptionObserved = false;
    try{
        NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
        threadPool.enqueueBatch(4u, [&](const usize index){
            if(index == 2u)
                throw __hidden_thread_pool_tests::s_BatchBuilderException;
            return [
                &invokedTasks,
                lifetime = __hidden_thread_pool_tests::TaskLifetimeProbe(retiredCaptures)
            ](){ invokedTasks.fetch_add(1u, MemoryOrder::release); };
        });
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_thread_pool_tests::s_BatchBuilderException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_EQ(retiredCaptures.load(MemoryOrder::acquire), 2u);
    EXPECT_EQ(invokedTasks.load(MemoryOrder::acquire), 0u);
}


TEST(ThreadPoolTests, InlineSelfDrainIsTerminalInEveryConfiguration){
    EXPECT_DEATH({
        NWB::Core::Alloc::ThreadPool threadPool(0u, CpuAffinity::Any);
        threadPool.enqueue([&](){ threadPool.drain(); });
    }, "");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

