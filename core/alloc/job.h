// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "persistent.h"
#include "scratch.h"
#include "thread.h"

#include <global/arena_object.h>
#include <global/exception.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class JobSystem : NoCopy{
public:
    struct JobHandle{
        static constexpr u32 s_InvalidIndex = static_cast<u32>(-1);

        u64 domainIdentity;
        u32 index;
        u32 generation;


        constexpr JobHandle()noexcept
            : domainIdentity(0u)
            , index(s_InvalidIndex)
            , generation(0u)
        {}
        constexpr JobHandle(u64 domainIdentity_, u32 index_, u32 generation_)noexcept
            : domainIdentity(domainIdentity_)
            , index(index_)
            , generation(generation_)
        {}
        constexpr JobHandle(const JobHandle&)noexcept = default;
        constexpr JobHandle(JobHandle&&)noexcept = default;
        constexpr JobHandle& operator=(const JobHandle&)noexcept = default;
        constexpr JobHandle& operator=(JobHandle&&)noexcept = default;

        inline bool valid()const{ return domainIdentity != 0u && index != s_InvalidIndex && generation != 0u; }
        inline explicit operator bool()const{ return valid(); }
    };


private:
    static constexpr usize s_JobInlineStorageBytes = 384;
    static constexpr u32 s_WorkFirstDepthLimit = 8;
    static constexpr usize s_DefaultMinimumArenaSize = 65536;
    static constexpr usize s_DefaultNodeCapacityPerThread = 256;
    static constexpr usize s_DefaultDependenciesPerNode = 4;
    static constexpr usize s_DefaultArenaOverheadBytes = 4096;


private:
    using JobFunction = InplaceFunction<s_JobInlineStorageBytes>;
    using ReadyBatch = Vector<JobHandle, ScratchArena>;


private:
    enum class JobState : u8{
        Free,
        Preparing,
        Releasing,
        Waiting,
        Scheduled,
        Executing,
        Canceled,
        CanceledNeedsCleanup,
        CanceledCleaning,
    };

    struct JobSignal{
        Atomic<u32> completedGeneration{ 0 };
    };

    struct JobNode{
        using DependencyList = Vector<JobHandle, PersistentArena>;

        JobFunction func;
        DependencyList dependents;
        JobSignal completionSignal;
        u32 generation = 1;
        u32 nextFreeIndex = JobHandle::s_InvalidIndex;
        usize remainingDependencies = 0u;
        JobState state = JobState::Free;


    public:
        inline explicit JobNode(PersistentArena& arena)noexcept
            : dependents(DependencyList::allocator_type(arena))
        {}
    };


private:
    using JobNodeList = Deque<JobNode, PersistentArena>;
    using DependencyNodeBatch = Vector<JobNode*, ScratchArena>;


private:
    class NodeReservation final : NoCopy{
        friend class JobSystem;


    public:
        inline NodeReservation(JobSystem& owner, JobHandle handle, JobNode& node)noexcept
            : m_owner(&owner)
            , m_handle(handle)
            , m_node(&node)
        {}
        inline ~NodeReservation()noexcept{
            if(m_owner)
                m_owner->releaseReservedNode(m_handle, *m_node);
        }


    public:
        inline void commit()noexcept{ m_owner = nullptr; }


    private:
        JobSystem* m_owner;
        JobHandle m_handle;
        JobNode* m_node;
    };

    class ExecutionLease final : NoCopy{
    public:
        inline explicit ExecutionLease(JobSystem& owner)noexcept
            : m_owner(&owner)
        {
            m_owner->m_outstandingWrapperCount.fetch_add(1u, MemoryOrder::acq_rel);
        }
        inline ExecutionLease(ExecutionLease&& rhs)noexcept
            : m_owner(rhs.m_owner)
            , m_executed(rhs.m_executed)
        {
            rhs.m_owner = nullptr;
            rhs.m_executed = false;
        }
        inline ~ExecutionLease()noexcept{
            if(m_owner)
                m_owner->retireExecutionWrapper(m_executed);
        }


    public:
        inline void markExecuted()noexcept{ m_executed = true; }


    private:
        JobSystem* m_owner;
        bool m_executed = false;
    };

    class ScopedExecutionIdentity final : NoCopy{
    public:
        inline ScopedExecutionIdentity(JobSystem& owner, JobHandle handle)noexcept
            : m_previousSystem(s_CurrentExecutionSystem)
            , m_previousHandle(s_CurrentExecutionHandle)
        {
            s_CurrentExecutionSystem = &owner;
            s_CurrentExecutionHandle = handle;
        }
        inline ~ScopedExecutionIdentity()noexcept{
            s_CurrentExecutionSystem = m_previousSystem;
            s_CurrentExecutionHandle = m_previousHandle;
        }


    private:
        JobSystem* m_previousSystem;
        JobHandle m_previousHandle;
    };


private:
    static inline u64 allocateDomainIdentity()noexcept{
        static Atomic<u64> nextDomainIdentity{ 1u };
        const u64 domainIdentity = nextDomainIdentity.fetch_add(1u, MemoryOrder::relaxed);
        NWB_ASSERT_MSG(domainIdentity != 0u, NWB_TEXT("JobSystem domain identity exhausted"));
        return domainIdentity;
    }

    static inline usize defaultNodeCapacityEstimate(u32 threadCount){
        const usize totalThreads = AddSize(static_cast<usize>(threadCount), 1);
        return SizeOf<s_DefaultNodeCapacityPerThread>(totalThreads);
    }

    static inline usize defaultArenaSize(u32 threadCount){
        const usize nodeCapacity = defaultNodeCapacityEstimate(threadCount);
        const usize nodeBytes = SizeOf<sizeof(JobNode)>(nodeCapacity);
        const usize dependencyCount = SizeOf<s_DefaultDependenciesPerNode>(nodeCapacity);
        const usize dependencyBytes = SizeOf<sizeof(JobHandle)>(dependencyCount);
        const usize freeListBytes = SizeOf<sizeof(u32)>(nodeCapacity);
        const usize total = AddSize(AddSize(AddSize(nodeBytes, dependencyBytes), freeListBytes), s_DefaultArenaOverheadBytes);
        const usize arenaSize = total > s_DefaultMinimumArenaSize ? total : s_DefaultMinimumArenaSize;
        return PersistentArena::StructureAlignedSize(arenaSize);
    }

    static inline usize resolveArenaSize(u32 threadCount, usize arenaSize){
        if(arenaSize > 0)
            return arenaSize;

        return defaultArenaSize(threadCount);
    }

    static inline bool isPendingState(JobState state)noexcept{
        return state == JobState::Waiting || state == JobState::Scheduled || state == JobState::Executing;
    }

public:
    inline explicit JobSystem(ThreadPool& pool, usize arenaSize = 0)
        : m_domainIdentity(allocateDomainIdentity())
        , m_pool(pool)
        , m_arena(ArenaScope::s_JobSystem, resolveArenaSize(pool.m_threadCount, arenaSize))
        , m_nodes(JobNodeList::allocator_type(m_arena))
    {}
    inline explicit JobSystem(u32 threadCount, u64 affinityMask = 0, usize arenaSize = 0)
        : m_domainIdentity(allocateDomainIdentity())
        , m_ownedPool(MakeUnique<ThreadPool>(threadCount, affinityMask, arenaSize))
        , m_pool(*m_ownedPool)
        , m_arena(ArenaScope::s_JobSystem, resolveArenaSize(threadCount, arenaSize))
        , m_nodes(JobNodeList::allocator_type(m_arena))
    {}
    inline explicit JobSystem(u32 threadCount, CpuAffinity::Enum affinity, usize arenaSize = 0)
        : m_domainIdentity(allocateDomainIdentity())
        , m_ownedPool(MakeUnique<ThreadPool>(threadCount, affinity, arenaSize))
        , m_pool(*m_ownedPool)
        , m_arena(ArenaScope::s_JobSystem, resolveArenaSize(threadCount, arenaSize))
        , m_nodes(JobNodeList::allocator_type(m_arena))
    {}

    inline ~JobSystem()noexcept{
        NWB_FATAL_ASSERT_MSG(
            !m_pool.isExecutingOnCurrentThread(),
            NWB_TEXT("JobSystem cannot be destroyed from an execution on its backing ThreadPool")
        );

        waitForPendingJobs();
        waitForExecutionWrappers();

        ExceptionPtr exception;
        bool exceptionObserved = false;
        {
            ScopedLock lock(m_mutex);
            exception = m_domainException;
            exceptionObserved = m_domainExceptionObserved;
        }
        if(exception && exceptionObserved)
            observeMatchingThreadPoolFailure(exception);
        NWB_FATAL_ASSERT_MSG(
            !exception || exceptionObserved || UncaughtExceptionCount() > 0,
            NWB_TEXT("JobSystem destruction encountered an unobserved task failure; call finish() at the owning boundary")
        );
    }


public:
    template<typename Func>
    inline JobHandle submit(Func&& task){
        throwIfDomainFailed();
        return submitWithDependencies(JobFunction(Forward<Func>(task)), nullptr, 0);
    }

    template<typename Func>
    inline JobHandle submit(Func&& task, JobHandle dependency){
        throwIfDomainFailed();
        return submitWithDependencies(JobFunction(Forward<Func>(task)), &dependency, 1);
    }

    template<typename Func>
    inline JobHandle submit(Func&& task, InitializerList<JobHandle> dependencies){
        throwIfDomainFailed();
        return submitWithDependencies(JobFunction(Forward<Func>(task)), dependencies.begin(), dependencies.size());
    }

    template<typename Func>
    inline JobHandle submit(Func&& task, const JobHandle* dependencies, usize dependencyCount){
        throwIfDomainFailed();
        return submitWithDependencies(JobFunction(Forward<Func>(task)), dependencies, dependencyCount);
    }

    template<typename Func>
    inline JobHandle then(JobHandle dependency, Func&& task){
        return submit(Forward<Func>(task), dependency);
    }

public:
    inline void drain()noexcept{
        NWB_FATAL_ASSERT_MSG(
            !m_pool.isExecutingOnCurrentThread(),
            NWB_TEXT("JobSystem cannot drain from an execution on its backing ThreadPool")
        );

        waitForPendingJobs();
        waitForExecutionWrappers();
        if(m_ownedPool)
            m_ownedPool->drain();
    }

    inline void finish(){
        if(m_pool.isExecutingOnCurrentThread())
            throw RuntimeException("JobSystem cannot finish from an execution on its backing ThreadPool");

        waitForPendingJobs();
        waitForExecutionWrappers();

        ExceptionPtr exception;
        {
            ScopedLock lock(m_mutex);
            exception = observeDomainExceptionLocked();
        }
        if(exception){
            observeMatchingThreadPoolFailure(exception);
            if(m_ownedPool)
                m_ownedPool->drain();
            RethrowException(exception);
        }
        if(m_ownedPool)
            m_ownedPool->finish();
    }

    inline void wait(JobHandle handle){
        if(!handle.valid())
            return;

        for(;;){
            JobSignal* completionSignal = nullptr;
            u32 completedGeneration = 0u;
            ExceptionPtr exception;
            bool rejectBackingPoolWait = false;

            {
                ScopedLock lock(m_mutex);

                exception = domainExceptionForCallerLocked();
                if(exception){
                    completionSignal = nullptr;
                }
                else if(handle.domainIdentity != m_domainIdentity || handle.index >= m_nodes.size()){
                    return;
                }
                else{
                    JobNode& node = m_nodes[handle.index];
                    if(node.generation != handle.generation || !isPendingState(node.state))
                        return;

                    completionSignal = &node.completionSignal;
                    completedGeneration = completionSignal->completedGeneration.load(MemoryOrder::acquire);
                    if(completedGeneration == handle.generation)
                        return;
                    rejectBackingPoolWait = m_pool.isExecutingOnCurrentThread();
                }
            }

            if(exception)
                rethrowObservedDomainFailure(exception);
            if(rejectBackingPoolWait)
                throw RuntimeException("JobSystem backing-pool execution cannot wait for a pending local job");

            NWB_ASSERT_MSG(completionSignal != nullptr, NWB_TEXT("JobSystem encountered a null completion signal"));
            completionSignal->completedGeneration.wait(completedGeneration, MemoryOrder::relaxed);
        }
    }

    inline void wait(InitializerList<JobHandle> handles){
        for(const JobHandle handle : handles)
            wait(handle);
    }

    inline void waitAll(){
        if(m_pool.isExecutingOnCurrentThread())
            throw RuntimeException("JobSystem cannot wait for its domain from an execution on its backing ThreadPool");

        waitForPendingJobs();

        ExceptionPtr exception;
        {
            ScopedLock lock(m_mutex);
            exception = observeDomainExceptionLocked();
        }
        if(exception)
            rethrowObservedDomainFailure(exception);
    }

    inline bool isComplete(JobHandle handle)const{
        if(!handle.valid())
            return true;

        ScopedLock lock(m_mutex);
        return !isPendingLocked(handle);
    }

private:
    inline JobHandle submitWithDependencies(JobFunction&& task, const JobHandle* dependencies, usize dependencyCount){
        if(!task)
            return JobHandle{};
        NWB_ASSERT_MSG(dependencies != nullptr || dependencyCount == 0u, NWB_TEXT("JobSystem dependencies cannot be null"));
        if(!dependencies && dependencyCount > 0u)
            return JobHandle{};

        ScratchArena scratchArena(ArenaScope::s_JobReadyBatch);
        DependencyNodeBatch resolvedDependencies{ DependencyNodeBatch::allocator_type(scratchArena) };
        resolvedDependencies.reserve(dependencyCount);

        JobNode* preparedNode = nullptr;
        const JobHandle output = reserveNode(preparedNode);
        if(!output.valid())
            return JobHandle{};

        NWB_ASSERT_MSG(preparedNode != nullptr, NWB_TEXT("JobSystem reserved a null job node"));
        NodeReservation reservation(*this, output, *preparedNode);
        preparedNode->func = Move(task);

        bool shouldSchedule = false;
        ExceptionPtr exception;

        {
            ScopedLock lock(m_mutex);

            exception = domainExceptionForCallerLocked();
            if(!exception){
                NWB_ASSERT_MSG(
                    preparedNode->generation == output.generation,
                    NWB_TEXT("JobSystem job reservation generation changed")
                );
                NWB_ASSERT_MSG(preparedNode->state == JobState::Preparing, NWB_TEXT("JobSystem job reservation changed state"));

                for(usize i = 0u; i < dependencyCount; ++i){
                    JobNode* dependencyNode = tryResolveNodeLocked(dependencies[i]);
                    if(dependencyNode)
                        resolvedDependencies.push_back(dependencyNode);
                }

                for(usize i = 0u; i < resolvedDependencies.size(); ++i){
                    JobNode* const dependencyNode = resolvedDependencies[i];
                    bool firstOccurrence = true;
                    for(usize j = 0u; j < i; ++j){
                        if(resolvedDependencies[j] == dependencyNode){
                            firstOccurrence = false;
                            break;
                        }
                    }
                    if(!firstOccurrence)
                        continue;

                    usize occurrenceCount = 1u;
                    for(usize j = i + 1u; j < resolvedDependencies.size(); ++j){
                        if(resolvedDependencies[j] == dependencyNode)
                            ++occurrenceCount;
                    }
                    dependencyNode->dependents.reserve(AddSize(dependencyNode->dependents.size(), occurrenceCount));
                }

                for(JobNode* dependencyNode : resolvedDependencies)
                    dependencyNode->dependents.push_back(output);

                preparedNode->remainingDependencies = resolvedDependencies.size();
                preparedNode->state = resolvedDependencies.empty() ? JobState::Scheduled : JobState::Waiting;
                m_pendingJobCount.fetch_add(1u, MemoryOrder::release);
                shouldSchedule = resolvedDependencies.empty();
            }
        }

        if(exception)
            rethrowObservedDomainFailure(exception);

        reservation.commit();

        if(shouldSchedule){
            try{
                enqueueExecution(output);
            }
            catch(...){
                const ExceptionPtr exception = CaptureCurrentException();
                failDomain(exception);
                rethrowObservedDomainFailure(exception);
            }
        }

        return output;
    }

    inline JobHandle reserveNode(JobNode*& outNode){
        outNode = nullptr;

        ExceptionPtr exception;
        JobHandle output;
        {
            ScopedLock lock(m_mutex);
            exception = domainExceptionForCallerLocked();
            if(!exception)
                output = acquireNodeLocked(outNode);
        }
        if(exception)
            rethrowObservedDomainFailure(exception);

        return output;
    }

    inline JobHandle acquireNodeLocked(JobNode*& outNode){
        u32 index = JobHandle::s_InvalidIndex;
        if(m_freeNodeHead != JobHandle::s_InvalidIndex){
            index = m_freeNodeHead;
            JobNode& freeNode = m_nodes[index];
            m_freeNodeHead = freeNode.nextFreeIndex;
        }
        else{
            if(m_nodes.size() >= static_cast<usize>(JobHandle::s_InvalidIndex)){
                NWB_ASSERT_MSG(false, NWB_TEXT("JobSystem exceeded maximum number of trackable jobs"));
                return JobHandle{};
            }

            index = static_cast<u32>(m_nodes.size());
            m_nodes.emplace_back(m_arena);
        }

        JobNode& node = m_nodes[index];
        NWB_ASSERT_MSG(node.state == JobState::Free, NWB_TEXT("JobSystem acquired a non-free job node"));
        NWB_ASSERT_MSG(!node.func, NWB_TEXT("JobSystem acquired a job node with a live task capture"));
        node.nextFreeIndex = JobHandle::s_InvalidIndex;
        node.remainingDependencies = 0u;
        node.state = JobState::Preparing;

        JobHandle output;
        output.domainIdentity = m_domainIdentity;
        output.index = index;
        output.generation = node.generation;
        outNode = &node;
        return output;
    }

    inline void releaseReservedNode(JobHandle handle, JobNode& node)noexcept{
        JobFunction task;
        {
            ScopedLock lock(m_mutex);
            NWB_ASSERT_MSG(
                handle.domainIdentity == m_domainIdentity,
                NWB_TEXT("JobSystem released a reservation from another domain")
            );
            NWB_ASSERT_MSG(
                handle.index < m_nodes.size() && &m_nodes[handle.index] == &node,
                NWB_TEXT("JobSystem released an invalid reservation")
            );
            NWB_ASSERT_MSG(node.generation == handle.generation, NWB_TEXT("JobSystem released a stale reservation"));
            NWB_ASSERT_MSG(node.state == JobState::Preparing, NWB_TEXT("JobSystem released a reservation in an invalid state"));
            node.state = JobState::Releasing;
        }

        task = Move(node.func);

        {
            ScopedLock lock(m_mutex);
            NWB_ASSERT_MSG(node.state == JobState::Releasing, NWB_TEXT("JobSystem reservation cleanup changed state"));
            recycleNodeLocked(handle.index, node);
        }
    }

    inline void recycleNodeLocked(u32 index, JobNode& node){
        NWB_ASSERT_MSG(!node.func, NWB_TEXT("JobSystem recycled a job node with a live task capture"));
        node.dependents.clear();
        node.remainingDependencies = 0u;
        node.state = JobState::Free;

        ++node.generation;
        if(node.generation == 0u)
            node.generation = 1u;

        node.nextFreeIndex = m_freeNodeHead;
        m_freeNodeHead = index;
    }

    inline bool isPendingLocked(JobHandle handle)const{
        if(handle.domainIdentity != m_domainIdentity || handle.index >= m_nodes.size())
            return false;

        const JobNode& node = m_nodes[handle.index];
        if(node.generation != handle.generation)
            return false;

        return isPendingState(node.state);
    }

    inline JobNode* tryResolveNodeLocked(JobHandle handle){
        if(!handle.valid() || handle.domainIdentity != m_domainIdentity || handle.index >= m_nodes.size())
            return nullptr;

        JobNode& node = m_nodes[handle.index];
        if(node.generation != handle.generation || !isPendingState(node.state))
            return nullptr;

        return &node;
    }

    inline void enqueueExecution(JobHandle handle){
        ExecutionLease lease(*this);
        m_pool.enqueue([this, handle, executionLease = Move(lease)]() mutable{
            executionLease.markExecuted();
            execute(handle);
        });
    }

    inline void enqueueExecutionBatch(const JobHandle* handles, usize handleCount){
        if(handleCount == 0)
            return;

        m_pool.enqueueBatch(handleCount, [this, handles](usize i){
            const JobHandle handle = handles[i];
            ExecutionLease lease(*this);
            return [this, handle, executionLease = Move(lease)]() mutable{
                executionLease.markExecuted();
                execute(handle);
            };
        });
    }

    inline void execute(JobHandle handle)noexcept{
        ScopedExecutionIdentity executionIdentity(*this, handle);

        try{
            executeJobs(handle);
        }
        catch(...){
            failDomain(CaptureCurrentException());
        }
    }

    inline void executeJobs(JobHandle handle){
        JobHandle current = handle;
        u32 workFirstDepth = 0;

        while(current.valid()){
            JobFunction task;
            JobNode* executingNode = nullptr;
            {
                ScopedLock lock(m_mutex);

                JobNode* node = tryResolveNodeLocked(current);
                if(!node || node->state != JobState::Scheduled)
                    return;

                node->state = JobState::Executing;
                executingNode = node;
            }

            task = Move(executingNode->func);
            NWB_ASSERT_MSG(static_cast<bool>(task), NWB_TEXT("JobSystem scheduled a job without a task"));
            task();

            const bool allowInline = workFirstDepth < s_WorkFirstDepthLimit;
            const JobHandle inlineContinuation = complete(current, allowInline);
            if(!inlineContinuation.valid())
                return;

            current = inlineContinuation;
            ++workFirstDepth;
        }
    }

    inline JobHandle complete(JobHandle handle, bool allowInline){
        ScratchArena scratchArena(ArenaScope::s_JobReadyBatch);
        ReadyBatch readyJobs{ReadyBatch::allocator_type(scratchArena)};
        JobSignal* completionSignal = nullptr;
        JobHandle inlineContinuation;
        bool pendingReachedZero = false;

        {
            ScopedLock lock(m_mutex);

            JobNode* node = tryResolveNodeLocked(handle);
            if(!node || node->state != JobState::Executing)
                return JobHandle{};

            const usize dependentCount = node->dependents.size();
            readyJobs.reserve(dependentCount);
            for(usize i = 0; i < dependentCount; ++i){
                const JobHandle dependentHandle = node->dependents[i];
                JobNode* dependentNode = tryResolveNodeLocked(dependentHandle);
                if(!dependentNode || dependentNode->state != JobState::Waiting)
                    continue;

                NWB_ASSERT_MSG(dependentNode->remainingDependencies > 0u, NWB_TEXT("JobSystem dependency counter underflow"));
                if(dependentNode->remainingDependencies == 0u)
                    continue;

                --dependentNode->remainingDependencies;
                if(dependentNode->remainingDependencies == 0u){
                    dependentNode->state = JobState::Scheduled;

                    if(allowInline && !inlineContinuation.valid()){
                        inlineContinuation = dependentHandle;
                        continue;
                    }

                    readyJobs.push_back(dependentHandle);
                }
            }

            completionSignal = &node->completionSignal;
            completionSignal->completedGeneration.store(handle.generation, MemoryOrder::release);
            recycleNodeLocked(handle.index, *node);

            const usize previousPendingCount = m_pendingJobCount.fetch_sub(1u, MemoryOrder::acq_rel);
            NWB_ASSERT_MSG(previousPendingCount > 0u, NWB_TEXT("JobSystem pending job counter underflow"));
            pendingReachedZero = previousPendingCount == 1u;
        }

        completionSignal->completedGeneration.notify_all();

        if(pendingReachedZero)
            m_pendingJobCount.notify_all();

        enqueueExecutionBatch(readyJobs.data(), readyJobs.size());

        return inlineContinuation;
    }

    [[nodiscard]] inline ExceptionPtr observeDomainExceptionLocked(){
        if(m_domainException)
            m_domainExceptionObserved = true;
        return m_domainException;
    }

    [[nodiscard]] inline ExceptionPtr domainExceptionForCallerLocked(){
        if(m_domainException && s_CurrentExecutionSystem != this)
            m_domainExceptionObserved = true;
        return m_domainException;
    }

    inline void throwIfDomainFailed(){
        ExceptionPtr exception;
        {
            ScopedLock lock(m_mutex);
            exception = domainExceptionForCallerLocked();
        }
        if(exception)
            rethrowObservedDomainFailure(exception);
    }

    inline void failDomain(const ExceptionPtr& exception)noexcept{
        NWB_ASSERT_MSG(static_cast<bool>(exception), NWB_TEXT("JobSystem cannot fail without an exception"));

        bool establishedFailure = false;
        usize nodeCount = 0u;
        {
            ScopedLock lock(m_mutex);
            if(!m_domainException){
                m_domainException = exception;
                establishedFailure = true;
                nodeCount = m_nodes.size();

                for(JobNode& node : m_nodes){
                    if(!isPendingState(node.state))
                        continue;

                    node.completionSignal.completedGeneration.store(node.generation, MemoryOrder::release);
                    node.state = node.state == JobState::Executing ? JobState::Canceled : JobState::CanceledNeedsCleanup;
                }
                m_pendingJobCount.store(0u, MemoryOrder::release);
            }
        }
        if(!establishedFailure)
            return;

        for(usize nodeIndex = 0u; nodeIndex < nodeCount; ++nodeIndex)
            m_nodes[nodeIndex].completionSignal.completedGeneration.notify_all();
        m_pendingJobCount.notify_all();

        for(usize nodeIndex = 0u; nodeIndex < nodeCount; ++nodeIndex){
            JobNode* cleanupNode = nullptr;
            {
                ScopedLock lock(m_mutex);
                JobNode& node = m_nodes[nodeIndex];
                if(node.state == JobState::CanceledNeedsCleanup){
                    node.state = JobState::CanceledCleaning;
                    cleanupNode = &node;
                }
            }
            if(!cleanupNode)
                continue;

            cleanupNode->func.reset();

            {
                ScopedLock lock(m_mutex);
                NWB_ASSERT_MSG(
                    cleanupNode->state == JobState::CanceledCleaning,
                    NWB_TEXT("JobSystem canceled capture cleanup changed state")
                );
                cleanupNode->state = JobState::Canceled;
            }
        }
    }

    [[nodiscard]] inline ExceptionPtr captureThreadPoolException()noexcept{
        ScopedLock lock(m_pool.m_taskMutex);
        return m_pool.m_taskException;
    }

    inline void observeMatchingThreadPoolFailure(const ExceptionPtr& domainException)noexcept{
        ScopedLock lock(m_pool.m_taskMutex);
        if(ExceptionPtrEqual(m_pool.m_taskException, domainException))
            m_pool.m_taskExceptionObserved = true;
    }

    [[noreturn]] inline void rethrowObservedDomainFailure(const ExceptionPtr& exception){
        if(s_CurrentExecutionSystem != this)
            observeMatchingThreadPoolFailure(exception);
        RethrowException(exception);
    }

    inline void retireExecutionWrapper(bool executed)noexcept{
        ScopedExecutionIdentity executionIdentity(*this, JobHandle{});

        if(!executed){
            const ExceptionPtr exception = captureThreadPoolException();
            if(exception)
                failDomain(exception);
        }

        ExceptionPtr observedDomainException;
        {
            ScopedLock lock(m_mutex);
            if(m_domainExceptionObserved)
                observedDomainException = m_domainException;
        }
        if(observedDomainException)
            observeMatchingThreadPoolFailure(observedDomainException);

        const usize previousCount = m_outstandingWrapperCount.fetch_sub(1u, MemoryOrder::acq_rel);
        NWB_ASSERT_MSG(previousCount > 0u, NWB_TEXT("JobSystem execution wrapper counter underflow"));
        if(previousCount == 1u)
            m_outstandingWrapperCount.notify_all();
    }

    inline void waitForPendingJobs()noexcept{
        usize current = m_pendingJobCount.load(MemoryOrder::acquire);
        while(current > 0u){
            m_pendingJobCount.wait(current, MemoryOrder::relaxed);
            current = m_pendingJobCount.load(MemoryOrder::acquire);
        }
    }

    inline void waitForExecutionWrappers()noexcept{
        usize current = m_outstandingWrapperCount.load(MemoryOrder::acquire);
        while(current > 0u){
            m_outstandingWrapperCount.wait(current, MemoryOrder::relaxed);
            current = m_outstandingWrapperCount.load(MemoryOrder::acquire);
        }
    }


private:
    inline static thread_local JobSystem* s_CurrentExecutionSystem = nullptr;
    inline static thread_local JobHandle s_CurrentExecutionHandle{};

    u64 m_domainIdentity;
    UniquePtr<ThreadPool> m_ownedPool;
    ThreadPool& m_pool;

    PersistentArena m_arena;
    JobNodeList m_nodes;
    u32 m_freeNodeHead = JobHandle::s_InvalidIndex;

    mutable Futex m_mutex;
    Atomic<usize> m_pendingJobCount{ 0 };
    Atomic<usize> m_outstandingWrapperCount{ 0 };
    ExceptionPtr m_domainException;
    bool m_domainExceptionObserved = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void FinishBorrowedSchedulerDomain(JobSystem& jobSystem, ThreadPool& threadPool){
    try{
        jobSystem.finish();
    }
    catch(...){
        const ExceptionPtr exception = CaptureCurrentException();
        threadPool.drain();
        RethrowException(exception);
    }

    threadPool.finish();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IJobScheduler{
public:
    inline explicit IJobScheduler(JobSystem& jobSystem)
        : m_jobSystem(jobSystem)
    {}


protected:
    template<typename Func>
    inline JobSystem::JobHandle scheduleJob(Func&& task){
        return m_jobSystem.submit(Forward<Func>(task));
    }

    template<typename Func>
    inline JobSystem::JobHandle scheduleJob(Func&& task, JobSystem::JobHandle dependency){
        return m_jobSystem.submit(Forward<Func>(task), dependency);
    }

    template<typename Func>
    inline JobSystem::JobHandle scheduleJob(Func&& task, InitializerList<JobSystem::JobHandle> dependencies){
        return m_jobSystem.submit(Forward<Func>(task), dependencies);
    }

    inline void waitJob(JobSystem::JobHandle handle){
        m_jobSystem.wait(handle);
    }

    inline void waitJobs(InitializerList<JobSystem::JobHandle> handles){
        m_jobSystem.wait(handles);
    }

    inline void waitAllJobs(){
        m_jobSystem.waitAll();
    }


private:
    JobSystem& m_jobSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

