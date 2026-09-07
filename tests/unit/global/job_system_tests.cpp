// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/scope_exit.h>

#include <core/alloc/job.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_job_system_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_TaskException = 0xE101u;
inline constexpr u32 s_PoolException = 0xE102u;
inline constexpr u32 s_TaskConstructionException = 0xE103u;
inline constexpr u32 s_UnrelatedUnwindException = 0xE104u;


struct ThrowingCopyTask{
    ThrowingCopyTask() = default;
    ThrowingCopyTask(const ThrowingCopyTask&){ throw s_TaskConstructionException; }
    ThrowingCopyTask(ThrowingCopyTask&&)noexcept = default;


    void operator()()const noexcept{}
};

struct BlockingCaptureRetirementTask{
    AtomicFlag* taskInvoked = nullptr;
    AtomicFlag* destructionEntered = nullptr;
    AtomicFlag* releaseDestruction = nullptr;


    BlockingCaptureRetirementTask(AtomicFlag& invoked, AtomicFlag& entered, AtomicFlag& release)noexcept
        : taskInvoked(&invoked)
        , destructionEntered(&entered)
        , releaseDestruction(&release)
    {}
    BlockingCaptureRetirementTask(const BlockingCaptureRetirementTask&) = delete;
    BlockingCaptureRetirementTask(BlockingCaptureRetirementTask&& rhs)noexcept
        : taskInvoked(rhs.taskInvoked)
        , destructionEntered(rhs.destructionEntered)
        , releaseDestruction(rhs.releaseDestruction)
    {
        rhs.taskInvoked = nullptr;
        rhs.destructionEntered = nullptr;
        rhs.releaseDestruction = nullptr;
    }
    ~BlockingCaptureRetirementTask()noexcept{
        if(!destructionEntered)
            return;

        destructionEntered->test_and_set(MemoryOrder::release);
        destructionEntered->notify_all();
        while(!releaseDestruction->test(MemoryOrder::acquire))
            releaseDestruction->wait(false, MemoryOrder::acquire);
    }


    void operator()()const noexcept{
        taskInvoked->test_and_set(MemoryOrder::release);
        taskInvoked->notify_all();
    }
};

struct CancellationProbe{
    Atomic<u32>* destructionCount;


    explicit CancellationProbe(Atomic<u32>& count)noexcept
        : destructionCount(&count)
    {}
    CancellationProbe(const CancellationProbe&) = delete;
    CancellationProbe(CancellationProbe&& rhs)noexcept
        : destructionCount(rhs.destructionCount)
    {
        rhs.destructionCount = nullptr;
    }
    ~CancellationProbe()noexcept{
        if(!destructionCount)
            return;

        destructionCount->fetch_add(1u, MemoryOrder::release);
        destructionCount->notify_all();
    }
};


struct ReentrantThrowingTask{
    NWB::Core::Alloc::JobSystem* owner;
    Atomic<u32>& rejectedAdmissions;
    Atomic<u32>& invokedTasks;


    ReentrantThrowingTask(NWB::Core::Alloc::JobSystem& scheduler, Atomic<u32>& rejected, Atomic<u32>& invoked)noexcept
        : owner(&scheduler)
        , rejectedAdmissions(rejected)
        , invokedTasks(invoked)
    {}
    ReentrantThrowingTask(ReentrantThrowingTask&& other)noexcept
        : owner(other.owner)
        , rejectedAdmissions(other.rejectedAdmissions)
        , invokedTasks(other.invokedTasks)
    {
        other.owner = nullptr;
    }
    ~ReentrantThrowingTask()noexcept{
        if(!owner)
            return;
        const auto job = owner->submit([&invoked = invokedTasks](){ invoked.fetch_add(1u, MemoryOrder::release); });
        if(!job.valid())
            rejectedAdmissions.fetch_add(1u, MemoryOrder::release);
    }


    void operator()(){ throw s_TaskException; }
};

static_assert(IsNothrowDestructible_V<NWB::Core::Alloc::JobSystem>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(JobSystemTests, WorkerJobExceptionTerminatesWithoutDeferredDelivery){
    EXPECT_DEATH({
        NWB::Core::Alloc::JobSystem jobSystem(1u, CpuAffinity::Any);
        const auto job = jobSystem.submit([](){ throw __hidden_job_system_tests::s_TaskException; });
        EXPECT_TRUE(job.valid());
        jobSystem.drain();
    }, "");
}


TEST(JobSystemTests, TaskConstructionUnwindPublishesNoJob){
    bool constructionExceptionObserved = false;
    try{
        NWB::Core::Alloc::JobSystem jobSystem(1u, CpuAffinity::Any);
        __hidden_job_system_tests::ThrowingCopyTask throwingTask;
        const auto rejectedJob = jobSystem.submit(throwingTask);
        EXPECT_FALSE(rejectedJob.valid());
    }
    catch(const u32 exception){
        constructionExceptionObserved = exception == __hidden_job_system_tests::s_TaskConstructionException;
    }
    EXPECT_TRUE(constructionExceptionObserved);
}


TEST(JobSystemTests, NestedInlineFailureCancelsBothExecutingJobsAndRetiresTheirCaptures){
    Atomic<u32> retiredCaptures{ 0u };
    u32 invokedTasks = 0u;
    bool exceptionObserved = false;
    try{
        NWB::Core::Alloc::JobSystem jobSystem(0u, CpuAffinity::Any);
        const auto outer = jobSystem.submit([
            &jobSystem,
            &invokedTasks,
            &retiredCaptures,
            lifetime = __hidden_job_system_tests::CancellationProbe(retiredCaptures)
        ](){
            ++invokedTasks;
            const auto inner = jobSystem.submit([
                &invokedTasks,
                lifetime = __hidden_job_system_tests::CancellationProbe(retiredCaptures)
            ](){
                ++invokedTasks;
                throw __hidden_job_system_tests::s_TaskException;
            });
            EXPECT_FALSE(inner.valid());
            ++invokedTasks;
        });
        EXPECT_FALSE(outer.valid());
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_job_system_tests::s_TaskException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_EQ(invokedTasks, 2u);
    EXPECT_EQ(retiredCaptures.load(MemoryOrder::acquire), 2u);
}


TEST(JobSystemTests, BorrowedPoolWorkerExceptionIsTerminalForTheProcess){
    EXPECT_DEATH({
        NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
        NWB::Core::Alloc::JobSystem jobSystem(threadPool);
        threadPool.enqueue([](){ throw __hidden_job_system_tests::s_PoolException; });
        NWB::Core::Alloc::FinishBorrowedSchedulerDomain(jobSystem, threadPool);
    }, "");
}


TEST(JobSystemTests, DuplicateAndMultipleDependenciesPublishOneContinuation){
    NWB::Core::Alloc::ThreadPool threadPool(3u, CpuAffinity::Any);
    NWB::Core::Alloc::JobSystem jobSystem(threadPool);
    Latch rootsReady(2u);
    AtomicFlag releaseRoots;
    Atomic<u32> rootCompletionCount{ 0u };

    const NWB::Core::Alloc::JobSystem::JobHandle firstRoot = jobSystem.submit([&](){
        rootsReady.count_down();
        rootsReady.wait();
        while(!releaseRoots.test(MemoryOrder::acquire))
            releaseRoots.wait(false, MemoryOrder::acquire);
        rootCompletionCount.fetch_add(1u, MemoryOrder::release);
    });
    const NWB::Core::Alloc::JobSystem::JobHandle secondRoot = jobSystem.submit([&](){
        rootsReady.count_down();
        rootsReady.wait();
        while(!releaseRoots.test(MemoryOrder::acquire))
            releaseRoots.wait(false, MemoryOrder::acquire);
        rootCompletionCount.fetch_add(1u, MemoryOrder::release);
    });
    rootsReady.wait();

    Atomic<u32> continuationInvocationCount{ 0u };
    Atomic<u32> rootsObservedByContinuation{ 0u };
    const NWB::Core::Alloc::JobSystem::JobHandle continuation = jobSystem.submit([&](){
        rootsObservedByContinuation.store(rootCompletionCount.load(MemoryOrder::acquire), MemoryOrder::release);
        continuationInvocationCount.fetch_add(1u, MemoryOrder::release);
    }, { firstRoot, firstRoot, secondRoot });

    Atomic<u32> tailInvocationCount{ 0u };
    const NWB::Core::Alloc::JobSystem::JobHandle tail = jobSystem.then(continuation, [&](){
        tailInvocationCount.fetch_add(1u, MemoryOrder::release);
    });

    releaseRoots.test_and_set(MemoryOrder::release);
    releaseRoots.notify_all();
    EXPECT_NO_THROW(jobSystem.wait(tail));
    EXPECT_NO_THROW(jobSystem.waitAll());
    EXPECT_EQ(rootCompletionCount.load(MemoryOrder::acquire), 2u);
    EXPECT_EQ(rootsObservedByContinuation.load(MemoryOrder::acquire), 2u);
    EXPECT_EQ(continuationInvocationCount.load(MemoryOrder::acquire), 1u);
    EXPECT_EQ(tailInvocationCount.load(MemoryOrder::acquire), 1u);
}


TEST(JobSystemTests, ForeignDependencyHandlesCannotAliasLocalJobs){
    NWB::Core::Alloc::ThreadPool threadPool(3u, CpuAffinity::Any);
    NWB::Core::Alloc::JobSystem firstJobSystem(threadPool);
    NWB::Core::Alloc::JobSystem secondJobSystem(threadPool);
    Latch blockersReady(2u);
    AtomicFlag releaseBlockers;

    const NWB::Core::Alloc::JobSystem::JobHandle firstBlocker = firstJobSystem.submit([&blockersReady, &releaseBlockers](){
        blockersReady.count_down();
        blockersReady.wait();
        while(!releaseBlockers.test(MemoryOrder::acquire))
            releaseBlockers.wait(false, MemoryOrder::acquire);
    });
    const NWB::Core::Alloc::JobSystem::JobHandle secondBlocker = secondJobSystem.submit([&blockersReady, &releaseBlockers](){
        blockersReady.count_down();
        blockersReady.wait();
        while(!releaseBlockers.test(MemoryOrder::acquire))
            releaseBlockers.wait(false, MemoryOrder::acquire);
    });
    blockersReady.wait();

    EXPECT_NE(firstBlocker.domainIdentity, secondBlocker.domainIdentity);
    AtomicFlag foreignDependentInvoked;
    const NWB::Core::Alloc::JobSystem::JobHandle foreignDependent = secondJobSystem.submit([&foreignDependentInvoked](){
        foreignDependentInvoked.test_and_set(MemoryOrder::release);
        foreignDependentInvoked.notify_all();
    }, firstBlocker);

    for(usize attempt = 0u; attempt < 65536u && !foreignDependentInvoked.test(MemoryOrder::acquire); ++attempt)
        YieldThread();
    const bool invokedBeforeLocalBlockerCompleted = foreignDependentInvoked.test(MemoryOrder::acquire);

    releaseBlockers.test_and_set(MemoryOrder::release);
    releaseBlockers.notify_all();
    EXPECT_NO_THROW(firstJobSystem.waitAll());
    EXPECT_NO_THROW(secondJobSystem.wait(foreignDependent));
    EXPECT_NO_THROW(secondJobSystem.waitAll());
    EXPECT_TRUE(invokedBeforeLocalBlockerCompleted);
}


TEST(JobSystemTests, DestructorWaitsForExecutionWrapperCaptureRetirement){
    NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
    UniquePtr<NWB::Core::Alloc::JobSystem> jobSystem = MakeUnique<NWB::Core::Alloc::JobSystem>(threadPool);
    AtomicFlag taskInvoked;
    AtomicFlag captureDestructionEntered;
    AtomicFlag releaseCaptureDestruction;

    const NWB::Core::Alloc::JobSystem::JobHandle job = jobSystem->submit(
        __hidden_job_system_tests::BlockingCaptureRetirementTask(
            taskInvoked,
            captureDestructionEntered,
            releaseCaptureDestruction
        )
    );
    EXPECT_TRUE(job.valid());
    while(!taskInvoked.test(MemoryOrder::acquire))
        taskInvoked.wait(false, MemoryOrder::acquire);
    while(!captureDestructionEntered.test(MemoryOrder::acquire))
        captureDestructionEntered.wait(false, MemoryOrder::acquire);

    AtomicFlag teardownStarted;
    AtomicFlag teardownReturned;
    JoiningThread teardownThread([&jobSystem, &teardownStarted, &teardownReturned](){
        teardownStarted.test_and_set(MemoryOrder::release);
        teardownStarted.notify_all();
        jobSystem.reset();
        teardownReturned.test_and_set(MemoryOrder::release);
        teardownReturned.notify_all();
    });
    while(!teardownStarted.test(MemoryOrder::acquire))
        teardownStarted.wait(false, MemoryOrder::acquire);
    for(usize attempt = 0u; attempt < 65536u && !teardownReturned.test(MemoryOrder::acquire); ++attempt)
        YieldThread();
    EXPECT_FALSE(teardownReturned.test(MemoryOrder::acquire));

    releaseCaptureDestruction.test_and_set(MemoryOrder::release);
    releaseCaptureDestruction.notify_all();
    teardownThread.join();
    EXPECT_TRUE(teardownReturned.test(MemoryOrder::acquire));
}


TEST(JobSystemTests, InlineCallbackExceptionReleasesItsCaptureBeforeLeavingTheOwningScope){
    Atomic<u32> retiredCaptures{ 0u };
    bool exceptionObserved = false;
    try{
        NWB::Core::Alloc::JobSystem jobSystem(0u, CpuAffinity::Any);
        const auto job = jobSystem.submit([lifetime = __hidden_job_system_tests::CancellationProbe(retiredCaptures)](){
            throw __hidden_job_system_tests::s_TaskException;
        });
        EXPECT_FALSE(job.valid());
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_job_system_tests::s_TaskException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_EQ(retiredCaptures.load(MemoryOrder::acquire), 1u);
}


TEST(JobSystemTests, BorrowedWholeDomainWaitsRejectEveryBackingPoolExecution){
    NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
    NWB::Core::Alloc::JobSystem jobSystem(threadPool);
    Atomic<u32> rejectionMask{ 0u };
    AtomicFlag workerReturned;

    threadPool.enqueue([&](){
        try{
            jobSystem.finish();
        }
        catch(const RuntimeException&){
            rejectionMask.fetch_or(1u, MemoryOrder::release);
        }
        catch(...){
            rejectionMask.fetch_or(4u, MemoryOrder::release);
        }

        try{
            jobSystem.waitAll();
        }
        catch(const RuntimeException&){
            rejectionMask.fetch_or(2u, MemoryOrder::release);
        }
        catch(...){
            rejectionMask.fetch_or(8u, MemoryOrder::release);
        }

        workerReturned.test_and_set(MemoryOrder::release);
        workerReturned.notify_all();
    });
    while(!workerReturned.test(MemoryOrder::acquire))
        workerReturned.wait(false, MemoryOrder::acquire);

    EXPECT_NO_THROW(jobSystem.finish());
    EXPECT_NO_THROW(threadPool.finish());
    EXPECT_EQ(rejectionMask.load(MemoryOrder::acquire), 3u);
}


TEST(JobSystemTests, BorrowedFinishRejectsCallerParallelExecutionWhileJobProgressIsPending){
    NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
    NWB::Core::Alloc::JobSystem jobSystem(threadPool);
    AtomicFlag jobEntered;
    AtomicFlag releaseJob;

    const NWB::Core::Alloc::JobSystem::JobHandle job = jobSystem.submit([&](){
        jobEntered.test_and_set(MemoryOrder::release);
        jobEntered.notify_all();
        while(!releaseJob.test(MemoryOrder::acquire))
            releaseJob.wait(false, MemoryOrder::acquire);
    });
    EXPECT_TRUE(job.valid());
    while(!jobEntered.test(MemoryOrder::acquire))
        jobEntered.wait(false, MemoryOrder::acquire);

    bool finishRejected = false;
    bool unexpectedException = false;
    try{
        threadPool.parallelFor(0u, 1u, [&](const usize){ jobSystem.finish(); });
    }
    catch(const RuntimeException&){
        finishRejected = true;
    }
    catch(...){
        unexpectedException = true;
    }

    releaseJob.test_and_set(MemoryOrder::release);
    releaseJob.notify_all();
    EXPECT_NO_THROW(jobSystem.finish());
    EXPECT_NO_THROW(threadPool.finish());
    EXPECT_TRUE(finishRejected);
    EXPECT_FALSE(unexpectedException);
}


TEST(JobSystemTests, CallerUnwindDrainsNonthrowingJobsAndTheirCaptures){
    AtomicFlag releaseJob;
    Atomic<u32> retiredCaptures{ 0u };
    bool exceptionObserved = false;
    try{
        NWB::Core::Alloc::JobSystem jobSystem(1u, CpuAffinity::Any);
        ScopeExit releaseWork([&]()noexcept{
            releaseJob.test_and_set(MemoryOrder::release);
            releaseJob.notify_all();
        });
        const auto job = jobSystem.submit([
            &releaseJob,
            lifetime = __hidden_job_system_tests::CancellationProbe(retiredCaptures)
        ](){
            while(!releaseJob.test(MemoryOrder::acquire))
                releaseJob.wait(false, MemoryOrder::acquire);
        });
        EXPECT_TRUE(job.valid());
        throw __hidden_job_system_tests::s_UnrelatedUnwindException;
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_job_system_tests::s_UnrelatedUnwindException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_EQ(retiredCaptures.load(MemoryOrder::acquire), 1u);
}


TEST(JobSystemTests, ExecutingJobSelfWaitTerminatesWithoutDeadlocking){
    EXPECT_DEATH({
        NWB::Core::Alloc::JobSystem::JobHandle self;
        AtomicFlag handlePublished;
        NWB::Core::Alloc::JobSystem jobSystem(1u, CpuAffinity::Any);
        self = jobSystem.submit([&](){
            while(!handlePublished.test(MemoryOrder::acquire))
                handlePublished.wait(false, MemoryOrder::acquire);
            jobSystem.wait(self);
        });
        EXPECT_TRUE(self.valid());
        handlePublished.test_and_set(MemoryOrder::release);
        handlePublished.notify_all();
        jobSystem.drain();
    }, "");
}


TEST(JobSystemTests, ExecutingJobPendingWaitTerminatesWithoutDeadlocking){
    EXPECT_DEATH({
        NWB::Core::Alloc::JobSystem::JobHandle target;
        AtomicFlag targetPublished;
        NWB::Core::Alloc::JobSystem jobSystem(1u, CpuAffinity::Any);
        const auto waiter = jobSystem.submit([&](){
            while(!targetPublished.test(MemoryOrder::acquire))
                targetPublished.wait(false, MemoryOrder::acquire);
            jobSystem.wait(target);
        });
        target = jobSystem.submit([]()noexcept{});
        EXPECT_TRUE(waiter.valid());
        EXPECT_TRUE(target.valid());
        targetPublished.test_and_set(MemoryOrder::release);
        targetPublished.notify_all();
        jobSystem.drain();
    }, "");
}


TEST(JobSystemTests, BackingPoolPendingJobWaitTerminatesWithoutDeadlocking){
    EXPECT_DEATH({
        NWB::Core::Alloc::JobSystem::JobHandle target;
        AtomicFlag targetPublished;
        NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
        NWB::Core::Alloc::JobSystem jobSystem(threadPool);
        threadPool.enqueue([&](){
            while(!targetPublished.test(MemoryOrder::acquire))
                targetPublished.wait(false, MemoryOrder::acquire);
            jobSystem.wait(target);
        });
        target = jobSystem.submit([]()noexcept{});
        EXPECT_TRUE(target.valid());
        targetPublished.test_and_set(MemoryOrder::release);
        targetPublished.notify_all();
        NWB::Core::Alloc::FinishBorrowedSchedulerDomain(jobSystem, threadPool);
    }, "");
}


TEST(JobSystemTests, ExecutingJobCanWaitForCompletedAndForeignHandles){
    Atomic<u32> invocationCount{ 0u };
    NWB::Core::Alloc::JobSystem::JobHandle completed;
    NWB::Core::Alloc::JobSystem::JobHandle foreign;
    NWB::Core::Alloc::JobSystem jobSystem(1u, CpuAffinity::Any);

    completed = jobSystem.submit([]()noexcept{});
    EXPECT_TRUE(completed.valid());
    EXPECT_NO_THROW(jobSystem.wait(completed));
    foreign = completed;
    ++foreign.domainIdentity;

    const NWB::Core::Alloc::JobSystem::JobHandle waiter = jobSystem.submit([&](){
        jobSystem.wait(completed);
        jobSystem.wait(foreign);
        invocationCount.fetch_add(1u, MemoryOrder::release);
    });
    EXPECT_TRUE(waiter.valid());
    EXPECT_NO_THROW(jobSystem.waitAll());
    EXPECT_EQ(invocationCount.load(MemoryOrder::acquire), 1u);
}


TEST(JobSystemTests, ExecutingJobDomainWaitTerminatesWithoutDeadlocking){
    EXPECT_DEATH({
        NWB::Core::Alloc::JobSystem jobSystem(1u, CpuAffinity::Any);
        const auto job = jobSystem.submit([&](){ jobSystem.waitAll(); });
        EXPECT_TRUE(job.valid());
        jobSystem.drain();
    }, "");
}


TEST(JobSystemTests, ExecutingJobCanSubmitNestedWork){
    Atomic<u32> invocationCount{ 0u };
    Atomic<u32> nestedHandleValid{ 0u };
    NWB::Core::Alloc::JobSystem jobSystem(1u, CpuAffinity::Any);

    const NWB::Core::Alloc::JobSystem::JobHandle root = jobSystem.submit([&](){
        invocationCount.fetch_add(1u, MemoryOrder::relaxed);
        const NWB::Core::Alloc::JobSystem::JobHandle nested = jobSystem.submit([&invocationCount](){
            invocationCount.fetch_add(1u, MemoryOrder::relaxed);
        });
        nestedHandleValid.store(nested.valid() ? 1u : 0u, MemoryOrder::release);
    });
    EXPECT_TRUE(root.valid());
    EXPECT_NO_THROW(jobSystem.waitAll());
    EXPECT_EQ(nestedHandleValid.load(MemoryOrder::acquire), 1u);
    EXPECT_EQ(invocationCount.load(MemoryOrder::relaxed), 2u);
}


TEST(JobSystemTests, InlineUnwindKeepsAnAlreadyRunningBorrowedPoolTaskAliveUntilDrain){
    AtomicFlag releaseTask;
    Atomic<u32> retiredCaptures{ 0u };
    bool exceptionObserved = false;
    try{
        NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
        NWB::Core::Alloc::JobSystem jobSystem(threadPool);
        ScopeExit drainDomain([&]()noexcept{
            releaseTask.test_and_set(MemoryOrder::release);
            releaseTask.notify_all();
            jobSystem.drain();
            threadPool.drain();
        });
        threadPool.enqueue([
            &releaseTask,
            lifetime = __hidden_job_system_tests::CancellationProbe(retiredCaptures)
        ](){
            while(!releaseTask.test(MemoryOrder::acquire))
                releaseTask.wait(false, MemoryOrder::acquire);
        });
        throw __hidden_job_system_tests::s_UnrelatedUnwindException;
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_job_system_tests::s_UnrelatedUnwindException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_EQ(retiredCaptures.load(MemoryOrder::acquire), 1u);
}


TEST(JobSystemTests, InlineFailurePublishesCancellationBeforeDestroyingTheRunningCapture){
    Atomic<u32> rejectedAdmissions{ 0u };
    Atomic<u32> invokedTasks{ 0u };
    bool exceptionObserved = false;
    try{
        NWB::Core::Alloc::JobSystem jobSystem(0u, CpuAffinity::Any);
        const auto job = jobSystem.submit(__hidden_job_system_tests::ReentrantThrowingTask(jobSystem, rejectedAdmissions, invokedTasks));
        EXPECT_FALSE(job.valid());
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_job_system_tests::s_TaskException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_EQ(rejectedAdmissions.load(MemoryOrder::acquire), 1u);
    EXPECT_EQ(invokedTasks.load(MemoryOrder::acquire), 0u);
}


TEST(JobSystemTests, InlineSelfDrainIsTerminalInEveryConfiguration){
    EXPECT_DEATH({
        NWB::Core::Alloc::JobSystem jobSystem(0u, CpuAffinity::Any);
        const auto job = jobSystem.submit([&](){ jobSystem.drain(); });
        EXPECT_FALSE(job.valid());
    }, "");
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

