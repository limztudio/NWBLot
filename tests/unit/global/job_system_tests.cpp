// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/exception.h>

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

struct ReentrantDestructionProbe{
    NWB::Core::Alloc::JobSystem* jobSystem = nullptr;
    NWB::Core::Alloc::JobSystem::JobHandle observedHandle;
    Atomic<u32>* destructionCount = nullptr;
    Atomic<u32>* observedCompletion = nullptr;


    ReentrantDestructionProbe(
        NWB::Core::Alloc::JobSystem& owner,
        NWB::Core::Alloc::JobSystem::JobHandle handle,
        Atomic<u32>& count,
        Atomic<u32>& completion
    )noexcept
        : jobSystem(&owner)
        , observedHandle(handle)
        , destructionCount(&count)
        , observedCompletion(&completion)
    {}
    ReentrantDestructionProbe(const ReentrantDestructionProbe&) = delete;
    ReentrantDestructionProbe(ReentrantDestructionProbe&& rhs)noexcept
        : jobSystem(rhs.jobSystem)
        , observedHandle(rhs.observedHandle)
        , destructionCount(rhs.destructionCount)
        , observedCompletion(rhs.observedCompletion)
    {
        rhs.jobSystem = nullptr;
        rhs.destructionCount = nullptr;
        rhs.observedCompletion = nullptr;
    }
    ~ReentrantDestructionProbe()noexcept{
        if(!jobSystem)
            return;

        observedCompletion->store(jobSystem->isComplete(observedHandle) ? 1u : 0u, MemoryOrder::release);
        destructionCount->fetch_add(1u, MemoryOrder::release);
        destructionCount->notify_all();
    }
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


static_assert(IsNothrowDestructible_V<NWB::Core::Alloc::JobSystem>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(JobSystemTests, TaskFailureCancelsDependentsAndPreservesTheExactFirstException){
    NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
    NWB::Core::Alloc::JobSystem jobSystem(threadPool);
    AtomicFlag throwingTaskEntered;
    AtomicFlag releaseThrowingTask;

    const NWB::Core::Alloc::JobSystem::JobHandle throwingJob = jobSystem.submit([&throwingTaskEntered, &releaseThrowingTask](){
        throwingTaskEntered.test_and_set(MemoryOrder::release);
        throwingTaskEntered.notify_all();
        while(!releaseThrowingTask.test(MemoryOrder::acquire))
            releaseThrowingTask.wait(false, MemoryOrder::acquire);
        throw __hidden_job_system_tests::s_TaskException;
    });
    while(!throwingTaskEntered.test(MemoryOrder::acquire))
        throwingTaskEntered.wait(false, MemoryOrder::acquire);

    Atomic<u32> dependentInvocationCount{ 0u };
    Atomic<u32> canceledCaptureDestructionCount{ 0u };
    Atomic<u32> cancellationObservedCompletion{ 0u };
    const NWB::Core::Alloc::JobSystem::JobHandle dependentJob = jobSystem.submit([
        lifetimeProbe = __hidden_job_system_tests::ReentrantDestructionProbe(
            jobSystem,
            throwingJob,
            canceledCaptureDestructionCount,
            cancellationObservedCompletion
        ),
        &dependentInvocationCount
    ]() mutable{
        if(lifetimeProbe.jobSystem)
            dependentInvocationCount.fetch_add(1u, MemoryOrder::relaxed);
    }, throwingJob);

    releaseThrowingTask.test_and_set(MemoryOrder::release);
    releaseThrowingTask.notify_all();

    bool waitExceptionObserved = false;
    try{
        jobSystem.wait(dependentJob);
    }
    catch(const u32 exception){
        waitExceptionObserved = exception == __hidden_job_system_tests::s_TaskException;
    }
    EXPECT_TRUE(waitExceptionObserved);

    u32 currentDestructionCount = canceledCaptureDestructionCount.load(MemoryOrder::acquire);
    while(currentDestructionCount == 0u){
        canceledCaptureDestructionCount.wait(currentDestructionCount, MemoryOrder::relaxed);
        currentDestructionCount = canceledCaptureDestructionCount.load(MemoryOrder::acquire);
    }
    EXPECT_EQ(dependentInvocationCount.load(MemoryOrder::relaxed), 0u);
    EXPECT_EQ(currentDestructionCount, 1u);
    EXPECT_EQ(cancellationObservedCompletion.load(MemoryOrder::acquire), 1u);
    EXPECT_TRUE(jobSystem.isComplete(throwingJob));
    EXPECT_TRUE(jobSystem.isComplete(dependentJob));

    Atomic<u32> rejectedTaskInvocationCount{ 0u };
    bool admissionExceptionObserved = false;
    try{
        const NWB::Core::Alloc::JobSystem::JobHandle rejectedJob = jobSystem.submit([&rejectedTaskInvocationCount](){
            rejectedTaskInvocationCount.fetch_add(1u, MemoryOrder::relaxed);
        });
        EXPECT_FALSE(rejectedJob.valid());
    }
    catch(const u32 exception){
        admissionExceptionObserved = exception == __hidden_job_system_tests::s_TaskException;
    }
    EXPECT_TRUE(admissionExceptionObserved);
    EXPECT_EQ(rejectedTaskInvocationCount.load(MemoryOrder::relaxed), 0u);

    bool waitAllExceptionObserved = false;
    try{
        jobSystem.waitAll();
    }
    catch(const u32 exception){
        waitAllExceptionObserved = exception == __hidden_job_system_tests::s_TaskException;
    }
    EXPECT_TRUE(waitAllExceptionObserved);
    EXPECT_NO_THROW(threadPool.finish());
}


TEST(JobSystemTests, TaskConstructionFailureLeavesTheSchedulerUsable){
    NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
    NWB::Core::Alloc::JobSystem jobSystem(threadPool);
    __hidden_job_system_tests::ThrowingCopyTask throwingTask;

    bool constructionExceptionObserved = false;
    try{
        const NWB::Core::Alloc::JobSystem::JobHandle rejectedJob = jobSystem.submit(throwingTask);
        EXPECT_FALSE(rejectedJob.valid());
    }
    catch(const u32 exception){
        constructionExceptionObserved = exception == __hidden_job_system_tests::s_TaskConstructionException;
    }
    EXPECT_TRUE(constructionExceptionObserved);

    Atomic<u32> invocationCount{ 0u };
    const NWB::Core::Alloc::JobSystem::JobHandle successfulJob = jobSystem.submit([&invocationCount](){
        invocationCount.fetch_add(1u, MemoryOrder::relaxed);
    });
    EXPECT_NO_THROW(jobSystem.wait(successfulJob));
    EXPECT_NO_THROW(jobSystem.waitAll());
    EXPECT_EQ(invocationCount.load(MemoryOrder::relaxed), 1u);
}


TEST(JobSystemTests, ThreadPoolPublicationFailureTerminalizesTheCommittedJob){
    NWB::Core::Alloc::ThreadPool threadPool(1u, CpuAffinity::Any);
    NWB::Core::Alloc::JobSystem jobSystem(threadPool);

    threadPool.enqueue([](){ throw __hidden_job_system_tests::s_PoolException; });
    bool poolExceptionObserved = false;
    try{
        threadPool.wait();
    }
    catch(const u32 exception){
        poolExceptionObserved = exception == __hidden_job_system_tests::s_PoolException;
    }
    EXPECT_TRUE(poolExceptionObserved);

    Atomic<u32> invocationCount{ 0u };
    bool submissionExceptionObserved = false;
    try{
        const NWB::Core::Alloc::JobSystem::JobHandle rejectedJob = jobSystem.submit([&invocationCount](){
            invocationCount.fetch_add(1u, MemoryOrder::relaxed);
        });
        EXPECT_FALSE(rejectedJob.valid());
    }
    catch(const u32 exception){
        submissionExceptionObserved = exception == __hidden_job_system_tests::s_PoolException;
    }
    EXPECT_TRUE(submissionExceptionObserved);
    EXPECT_EQ(invocationCount.load(MemoryOrder::relaxed), 0u);

    bool waitExceptionObserved = false;
    try{
        jobSystem.waitAll();
    }
    catch(const u32 exception){
        waitExceptionObserved = exception == __hidden_job_system_tests::s_PoolException;
    }
    EXPECT_TRUE(waitExceptionObserved);
}


TEST(JobSystemTests, DistinctJobAndSharedPoolFailuresRemainIndependentlyObservable){
    NWB::Core::Alloc::ThreadPool threadPool(2u, CpuAffinity::Any);
    NWB::Core::Alloc::JobSystem jobSystem(threadPool);
    AtomicFlag throwingJobEntered;
    AtomicFlag releaseThrowingJob;

    const NWB::Core::Alloc::JobSystem::JobHandle throwingJob = jobSystem.submit([&](){
        throwingJobEntered.test_and_set(MemoryOrder::release);
        throwingJobEntered.notify_all();
        while(!releaseThrowingJob.test(MemoryOrder::acquire))
            releaseThrowingJob.wait(false, MemoryOrder::acquire);
        throw __hidden_job_system_tests::s_TaskException;
    });
    EXPECT_TRUE(throwingJob.valid());
    while(!throwingJobEntered.test(MemoryOrder::acquire))
        throwingJobEntered.wait(false, MemoryOrder::acquire);

    AtomicFlag dependentInvoked;
    AtomicFlag canceledCaptureDestructionEntered;
    AtomicFlag releaseCanceledCaptureDestruction;
    const NWB::Core::Alloc::JobSystem::JobHandle dependentJob = jobSystem.submit(
        __hidden_job_system_tests::BlockingCaptureRetirementTask(
            dependentInvoked,
            canceledCaptureDestructionEntered,
            releaseCanceledCaptureDestruction
        ),
        throwingJob
    );
    EXPECT_TRUE(dependentJob.valid());

    threadPool.enqueue([&canceledCaptureDestructionEntered](){
        while(!canceledCaptureDestructionEntered.test(MemoryOrder::acquire))
            canceledCaptureDestructionEntered.wait(false, MemoryOrder::acquire);
        throw __hidden_job_system_tests::s_PoolException;
    });
    Atomic<u32> canceledPoolCaptureCount{ 0u };
    threadPool.enqueue([
        cancellationProbe = __hidden_job_system_tests::CancellationProbe(canceledPoolCaptureCount)
    ]() mutable{});

    releaseThrowingJob.test_and_set(MemoryOrder::release);
    releaseThrowingJob.notify_all();
    while(!canceledCaptureDestructionEntered.test(MemoryOrder::acquire))
        canceledCaptureDestructionEntered.wait(false, MemoryOrder::acquire);
    u32 currentCanceledPoolCaptureCount = canceledPoolCaptureCount.load(MemoryOrder::acquire);
    while(currentCanceledPoolCaptureCount == 0u){
        canceledPoolCaptureCount.wait(currentCanceledPoolCaptureCount, MemoryOrder::relaxed);
        currentCanceledPoolCaptureCount = canceledPoolCaptureCount.load(MemoryOrder::acquire);
    }

    releaseCanceledCaptureDestruction.test_and_set(MemoryOrder::release);
    releaseCanceledCaptureDestruction.notify_all();

    bool jobExceptionObserved = false;
    try{
        jobSystem.finish();
    }
    catch(const u32 exception){
        jobExceptionObserved = exception == __hidden_job_system_tests::s_TaskException;
    }
    EXPECT_TRUE(jobExceptionObserved);

    bool poolExceptionObserved = false;
    try{
        threadPool.finish();
    }
    catch(const u32 exception){
        poolExceptionObserved = exception == __hidden_job_system_tests::s_PoolException;
    }
    EXPECT_TRUE(poolExceptionObserved);
    EXPECT_FALSE(dependentInvoked.test(MemoryOrder::acquire));
    EXPECT_EQ(currentCanceledPoolCaptureCount, 1u);
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


TEST(JobSystemTests, FinishPropagatesTheExactTaskException){
    NWB::Core::Alloc::JobSystem jobSystem(1u, CpuAffinity::Any);
    const NWB::Core::Alloc::JobSystem::JobHandle job = jobSystem.submit([](){
        throw __hidden_job_system_tests::s_TaskException;
    });
    EXPECT_TRUE(job.valid());

    bool exceptionObserved = false;
    try{
        jobSystem.finish();
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_job_system_tests::s_TaskException;
    }
    EXPECT_TRUE(exceptionObserved);
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


TEST(JobSystemTests, TaskExceptionDoesNotReplaceActiveUnwind){
    bool exceptionObserved = false;
    try{
        NWB::Core::Alloc::JobSystem jobSystem(1u, CpuAffinity::Any);
        const NWB::Core::Alloc::JobSystem::JobHandle job = jobSystem.submit([](){
            throw __hidden_job_system_tests::s_TaskException;
        });
        EXPECT_TRUE(job.valid());
        throw __hidden_job_system_tests::s_UnrelatedUnwindException;
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_job_system_tests::s_UnrelatedUnwindException;
    }
    EXPECT_TRUE(exceptionObserved);
}


TEST(JobSystemTests, ExecutingJobCannotWaitForItself){
    NWB::Core::Alloc::JobSystem::JobHandle self;
    AtomicFlag handlePublished;
    NWB::Core::Alloc::JobSystem jobSystem(1u, CpuAffinity::Any);

    self = jobSystem.submit([&jobSystem, &self, &handlePublished](){
        while(!handlePublished.test(MemoryOrder::acquire))
            handlePublished.wait(false, MemoryOrder::acquire);
        jobSystem.wait(self);
    });
    EXPECT_TRUE(self.valid());
    handlePublished.test_and_set(MemoryOrder::release);
    handlePublished.notify_all();

    bool exceptionObserved = false;
    try{
        jobSystem.waitAll();
    }
    catch(const RuntimeException&){
        exceptionObserved = true;
    }
    EXPECT_TRUE(exceptionObserved);
}


TEST(JobSystemTests, ExecutingJobCannotWaitForAnotherPendingJobInItsOwnDomain){
    NWB::Core::Alloc::JobSystem::JobHandle target;
    AtomicFlag targetPublished;
    NWB::Core::Alloc::JobSystem jobSystem(1u, CpuAffinity::Any);

    const NWB::Core::Alloc::JobSystem::JobHandle waiter = jobSystem.submit([&](){
        while(!targetPublished.test(MemoryOrder::acquire))
            targetPublished.wait(false, MemoryOrder::acquire);
        jobSystem.wait(target);
    });
    target = jobSystem.submit([]()noexcept{});
    EXPECT_TRUE(waiter.valid());
    EXPECT_TRUE(target.valid());
    targetPublished.test_and_set(MemoryOrder::release);
    targetPublished.notify_all();

    bool exceptionObserved = false;
    try{
        jobSystem.waitAll();
    }
    catch(const RuntimeException&){
        exceptionObserved = true;
    }
    EXPECT_TRUE(exceptionObserved);
}


TEST(JobSystemTests, BackingPoolTaskCannotWaitForPendingJobHandle){
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

    bool exceptionObserved = false;
    try{
        jobSystem.finish();
    }
    catch(const RuntimeException&){
        exceptionObserved = true;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_NO_THROW(threadPool.finish());
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


TEST(JobSystemTests, ExecutingJobCannotWaitForItsOwnDomain){
    NWB::Core::Alloc::JobSystem jobSystem(1u, CpuAffinity::Any);
    const NWB::Core::Alloc::JobSystem::JobHandle job = jobSystem.submit([&jobSystem](){ jobSystem.waitAll(); });
    EXPECT_TRUE(job.valid());

    bool exceptionObserved = false;
    try{
        jobSystem.waitAll();
    }
    catch(const RuntimeException&){
        exceptionObserved = true;
    }
    EXPECT_TRUE(exceptionObserved);
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


TEST(JobSystemTests, SameSystemWorkerAdmissionDoesNotObserveTheDomainFailure){
    Latch workersReady(2u);
    AtomicFlag releaseThrowingTask;
    AtomicFlag unexpectedAdmission;
    Atomic<u32> canceledCaptureCount{ 0u };

    bool exceptionObserved = false;
    try{
        NWB::Core::Alloc::JobSystem jobSystem(2u, CpuAffinity::Any);
        const NWB::Core::Alloc::JobSystem::JobHandle throwingJob = jobSystem.submit([&](){
            workersReady.count_down();
            workersReady.wait();
            while(!releaseThrowingTask.test(MemoryOrder::acquire))
                releaseThrowingTask.wait(false, MemoryOrder::acquire);
            throw __hidden_job_system_tests::s_TaskException;
        });
        const NWB::Core::Alloc::JobSystem::JobHandle admittingJob = jobSystem.submit([&](){
            workersReady.count_down();
            workersReady.wait();

            u32 currentCaptureCount = canceledCaptureCount.load(MemoryOrder::acquire);
            while(currentCaptureCount == 0u){
                canceledCaptureCount.wait(currentCaptureCount, MemoryOrder::relaxed);
                currentCaptureCount = canceledCaptureCount.load(MemoryOrder::acquire);
            }

            const NWB::Core::Alloc::JobSystem::JobHandle unexpectedJob = jobSystem.submit([]()noexcept{});
            if(unexpectedJob.valid()){
                unexpectedAdmission.test_and_set(MemoryOrder::release);
                unexpectedAdmission.notify_all();
            }
        });
        EXPECT_TRUE(throwingJob.valid());
        EXPECT_TRUE(admittingJob.valid());
        workersReady.wait();

        const NWB::Core::Alloc::JobSystem::JobHandle canceledJob = jobSystem.submit([
            cancellationProbe = __hidden_job_system_tests::CancellationProbe(canceledCaptureCount)
        ]() mutable{});
        EXPECT_TRUE(canceledJob.valid());
        releaseThrowingTask.test_and_set(MemoryOrder::release);
        releaseThrowingTask.notify_all();
        jobSystem.finish();
    }
    catch(const u32 exception){
        exceptionObserved = exception == __hidden_job_system_tests::s_TaskException;
    }
    EXPECT_TRUE(exceptionObserved);
    EXPECT_FALSE(unexpectedAdmission.test(MemoryOrder::acquire));
    EXPECT_EQ(canceledCaptureCount.load(MemoryOrder::acquire), 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

