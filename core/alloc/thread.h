// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "persistent.h"
#include "scratch.h"
#include "arena_names.h"

#include <global/cpu_topology.h>
#include <global/scope_exit.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class JobSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ThreadPool : NoCopy{
    friend class JobSystem;


private:
    static constexpr usize s_TaskInlineStorageBytes = 128;
    static constexpr usize s_ChunkOversubscription = 4;
    static constexpr usize s_TaskQueueSlotsPerThread = 8;
    static constexpr usize s_DefaultArenaScratchBytes = 4096;
    static constexpr usize s_MinDefaultArenaBytes = 32768;


private:
    using TaskFunction = InplaceFunction<s_TaskInlineStorageBytes>;


private:
    struct TaskItem{
        TaskFunction func;
        TaskItem* next = nullptr;
    };

    struct ScopedParallelForExecution;

    struct ParallelForDesc{
        void (*invoke)(const void* functor, usize chunkBegin, usize chunkEnd);
        const void* functor;
        ThreadPool* owner;
        const ScopedParallelForExecution* parentExecution;
        Atomic<usize> nextChunk{ 0 };
        usize numChunks;
        usize begin;
        usize chunkSize;
        usize remainder;
        Latch* done;
        Atomic<bool> cancelRequested{ false };
    };

    using WorkerList = Vector<JoiningThread, PersistentArena>;
    using TaskQueue = Deque<TaskItem*, PersistentArena>;
    using TaskBatch = Vector<TaskFunction, ScratchArena>;
    using TaskNodeBatch = Vector<TaskItem*, ScratchArena>;
    using TaskNodeAllocator = ContainerDetail::ArenaAllocatorFor_T<TaskItem, PersistentArena>;

    struct ScopedParallelForExecution{
        ThreadPool* const owner;
        const ScopedParallelForExecution* const ancestor;
        const ScopedParallelForExecution* const restore;


        inline ScopedParallelForExecution(ThreadPool* executionOwner, const ScopedParallelForExecution* parent)noexcept
            : owner(executionOwner)
            , ancestor(parent)
            , restore(s_CurrentParallelForExecution)
        {
            s_CurrentParallelForExecution = this;
        }

        inline ~ScopedParallelForExecution(){
            s_CurrentParallelForExecution = restore;
        }

        ScopedParallelForExecution(const ScopedParallelForExecution&) = delete;
        ScopedParallelForExecution& operator=(const ScopedParallelForExecution&) = delete;
    };

    struct ScopedWorkerExecution{
        ThreadPool* previousPool;
        usize previousIndex;


        inline ScopedWorkerExecution(ThreadPool* owner, const usize workerIndex)noexcept
            : previousPool(s_CurrentWorkerPool)
            , previousIndex(s_CurrentWorkerIndex)
        {
            s_CurrentWorkerPool = owner;
            s_CurrentWorkerIndex = workerIndex;
        }

        inline ~ScopedWorkerExecution(){
            s_CurrentWorkerPool = previousPool;
            s_CurrentWorkerIndex = previousIndex;
        }

        ScopedWorkerExecution(const ScopedWorkerExecution&) = delete;
        ScopedWorkerExecution& operator=(const ScopedWorkerExecution&) = delete;
    };


private:
    static inline u64 allocateDomainIdentity()noexcept{
        static Atomic<u64> nextDomainIdentity{ 1u };
        const u64 domainIdentity = nextDomainIdentity.fetch_add(1u, MemoryOrder::relaxed);
        NWB_ASSERT_MSG(domainIdentity != 0u, NWB_TEXT("ThreadPool domain identity exhausted"));
        return domainIdentity;
    }

    static inline usize defaultArenaSize(u32 threadCount){
        const usize workerCount = AddSize(static_cast<usize>(threadCount), 1);
        const usize workerBytes = SizeOf<sizeof(JoiningThread)>(workerCount);
        const usize taskCount = SizeOf<s_TaskQueueSlotsPerThread>(static_cast<usize>(threadCount));
        const usize taskBytes = SizeOf<sizeof(TaskItem)>(taskCount);
        const usize total = AddSize(AddSize(workerBytes, taskBytes), s_DefaultArenaScratchBytes);
        return PersistentArena::StructureAlignedSize(total > s_MinDefaultArenaBytes ? total : s_MinDefaultArenaBytes);
    }

    static inline usize computeChunkCount(usize maxChunks, usize totalThreads){
        usize targetChunks = totalThreads;
        if(totalThreads <= static_cast<usize>(-1) / s_ChunkOversubscription)
            targetChunks = totalThreads * s_ChunkOversubscription;

        return (maxChunks < targetChunks) ? maxChunks : targetChunks;
    }

    template<typename Func>
    static inline void runSerialRange(usize begin, usize end, const Func& func){
        for(usize i = begin; i < end; ++i)
            func(i);
    }

    static inline void resetTaskCaptures(TaskItem* task)noexcept{
        while(task){
            task->func.reset();
            task = task->next;
        }
    }

    static inline void processParallelFor(ParallelForDesc* pf){
        ScopedParallelForExecution executionScope(pf->owner, pf->parentExecution);
        for(;;){
            const usize c = pf->nextChunk.fetch_add(1, MemoryOrder::relaxed);
            if(c >= pf->numChunks)
                break;

            ScopeExit completeChunk([&]()noexcept{ pf->done->count_down(); });
            const usize cb = pf->begin + c * pf->chunkSize + ((c < pf->remainder) ? c : pf->remainder);
            const usize ce = cb + pf->chunkSize + ((c < pf->remainder) ? 1 : 0);
            if(!pf->cancelRequested.load(MemoryOrder::acquire))
                pf->invoke(pf->functor, cb, ce);
        }
    }

    [[nodiscard]] inline bool mustRunParallelForSerially()const noexcept{
        u64 greatestAncestorDomainIdentity = 0u;
        for(const ScopedParallelForExecution* execution = s_CurrentParallelForExecution; execution; execution = execution->ancestor)
            greatestAncestorDomainIdentity = Max(greatestAncestorDomainIdentity, execution->owner->m_domainIdentity);
        return greatestAncestorDomainIdentity != 0u && m_domainIdentity <= greatestAncestorDomainIdentity;
    }

    [[nodiscard]] inline bool isExecutingParallelForOnCurrentThread()const noexcept{
        for(const ScopedParallelForExecution* execution = s_CurrentParallelForExecution; execution; execution = execution->ancestor){
            if(execution->owner == this)
                return true;
        }
        return false;
    }

    [[nodiscard]] inline bool isExecutingOnCurrentThread()const noexcept{
        return s_CurrentWorkerPool == this || isExecutingParallelForOnCurrentThread();
    }

    template<typename Func>
    inline void runParallelForChunks(usize begin, usize end, usize count, usize numChunks, const Func& func){
        if(numChunks <= 1){
            runSerialRange(begin, end, func);
            return;
        }
        const usize chunkSize = count / numChunks;
        const usize remainder = count % numChunks;
        dispatchParallelFor(begin, end, func, numChunks, chunkSize, remainder);
    }

    [[nodiscard]] inline TaskItem* createTaskNode(TaskFunction&& function){
        static_assert(IsNothrowMoveConstructible_V<TaskFunction>);
        TaskItem* task;
        {
            ScopedLock lock(m_taskMutex);
            TaskNodeAllocator allocator(m_arena);
            task = allocator.allocate(1u);
        }
        new(task) TaskItem{ Move(function) };
        return task;
    }

    inline void createTaskNodes(TaskBatch& functions, TaskNodeBatch& tasks){
        static_assert(IsNothrowMoveConstructible_V<TaskFunction>);
        ScopeExit releaseStorage([&]()noexcept{
            ScopedLock lock(m_taskMutex);
            TaskNodeAllocator allocator(m_arena);
            for(TaskItem* task : tasks)
                allocator.deallocate(task, 1u);
            tasks.clear();
        });
        {
            ScopedLock lock(m_taskMutex);
            TaskNodeAllocator allocator(m_arena);
            for(usize i = 0u; i < functions.size(); ++i)
                tasks.push_back(allocator.allocate(1u));
        }
        for(usize i = 0u; i < functions.size(); ++i)
            new(tasks[i]) TaskItem{ Move(functions[i]) };
        releaseStorage.release();
    }

    inline void deallocateTaskNodesLocked(TaskItem* task)noexcept{
        TaskNodeAllocator allocator(m_arena);
        while(task){
            TaskItem* next = task->next;
            task->~TaskItem();
            allocator.deallocate(task, 1u);
            task = next;
        }
    }

    inline void releaseTaskNodes(TaskItem* task)noexcept{
        resetTaskCaptures(task);
        ScopedLock lock(m_taskMutex);
        deallocateTaskNodesLocked(task);
    }

    inline void waitTaskDomain()noexcept{
        UniqueLock taskLock(m_taskMutex);
        m_taskStateChanged.wait(taskLock, [this](){ return m_pendingCount == 0u && m_activeTaskWorkers == 0u; });
    }


public:
    inline explicit ThreadPool(u32 threadCount, u64 affinityMask = 0, usize arenaSize = 0)
        : m_domainIdentity(allocateDomainIdentity())
        , m_arena(ArenaScope::s_ThreadPool, arenaSize > 0 ? arenaSize : defaultArenaSize(threadCount))
        , m_tasks(TaskQueue::allocator_type(m_arena))
        , m_threadCount(threadCount)
        , m_workers(WorkerList::allocator_type(m_arena))
    {
        m_workers.reserve(threadCount);
        for(u32 i = 0; i < threadCount; ++i){
            m_workers.emplace_back([this, affinityMask, workerIndex = static_cast<usize>(i) + 1u](const StopToken& stopToken){
                workerLoop(stopToken, affinityMask, workerIndex);
            });
        }
    }
    inline explicit ThreadPool(u32 threadCount, CpuAffinity::Enum affinity, usize arenaSize = 0)
        : ThreadPool(threadCount, QueryCpuAffinityMask(affinity), arenaSize)
    {}
    inline ~ThreadPool()noexcept{
        drain();
    }


public:
    template<typename Func>
    inline void enqueue(Func&& task){
        TaskFunction function(Forward<Func>(task));
        if(m_threadCount == 0){
            ScopedWorkerExecution workerExecution(this, 0u);
            function();
            function.reset();
            return;
        }

        TaskItem* const item = createTaskNode(Move(function));
        ScopeExit releaseNode([&]()noexcept{ releaseTaskNodes(item); });
        {
            ScopedLock lock(m_taskMutex);
            const usize pendingCount = AddSize(m_pendingCount, 1u);
            m_tasks.push_back(item);
            m_pendingCount = pendingCount;
        }
        releaseNode.release();
        m_taskAvailable.notify_one();
    }

    template<typename TaskBuilder>
    inline void enqueueBatch(usize taskCount, const TaskBuilder& taskBuilder){
        if(taskCount == 0)
            return;

        ScratchArena scratchArena(ArenaScope::s_ThreadPoolBatch);
        TaskBatch preparedTasks{ TaskBatch::allocator_type(scratchArena) };
        preparedTasks.reserve(taskCount);
        for(usize i = 0u; i < taskCount; ++i)
            preparedTasks.push_back(TaskFunction(taskBuilder(i)));

        if(m_threadCount == 0u){
            ScopedWorkerExecution workerExecution(this, 0u);
            for(TaskFunction& function : preparedTasks)
                function();
            return;
        }

        TaskNodeBatch preparedNodes{ TaskNodeBatch::allocator_type(scratchArena) };
        preparedNodes.reserve(taskCount);
        createTaskNodes(preparedTasks, preparedNodes);
        ScopeExit releaseNodes([&]()noexcept{
            TaskItem* taskList = nullptr;
            for(TaskItem* task : preparedNodes){
                task->next = taskList;
                taskList = task;
            }
            releaseTaskNodes(taskList);
        });
        {
            ScopedLock lock(m_taskMutex);
            const usize pendingCount = AddSize(m_pendingCount, taskCount);
            usize publishedCount = 0u;
            ScopeExit undoPublication([&]()noexcept{
                while(publishedCount > 0u){
                    m_tasks.pop_back();
                    --publishedCount;
                }
            });
            for(TaskItem* task : preparedNodes){
                m_tasks.push_back(task);
                ++publishedCount;
            }
            m_pendingCount = pendingCount;
            undoPublication.release();
        }
        releaseNodes.release();

        const usize wakeCount = Min(taskCount, static_cast<usize>(m_threadCount));
        for(usize i = 0u; i < wakeCount; ++i)
            m_taskAvailable.notify_one();
    }


    template<typename Func, typename Callback>
    inline void enqueue(Func&& task, Callback&& onComplete){
        enqueue([taskFn = Forward<Func>(task), onCompleteFn = Forward<Callback>(onComplete)]() mutable{
            taskFn();
            onCompleteFn();
        });
    }

public:
    template<typename Func>
    inline void parallelFor(usize begin, usize end, const Func& func){
        if(begin >= end)
            return;

        const usize count = end - begin;

        if(m_threadCount == 0 || count == 1 || mustRunParallelForSerially()){
            ScopedParallelForExecution executionScope(this, s_CurrentParallelForExecution);
            runSerialRange(begin, end, func);
            return;
        }

        const usize totalThreads = static_cast<usize>(m_threadCount) + 1;
        runParallelForChunks(begin, end, count, computeChunkCount(count, totalThreads), func);
    }

    template<typename Func>
    inline void parallelFor(usize begin, usize end, usize grainSize, const Func& func){
        if(begin >= end)
            return;

        const usize count = end - begin;
        const usize effectiveGrainSize = grainSize > 0 ? grainSize : 1;

        if(m_threadCount == 0 || count <= effectiveGrainSize || mustRunParallelForSerially()){
            ScopedParallelForExecution executionScope(this, s_CurrentParallelForExecution);
            runSerialRange(begin, end, func);
            return;
        }

        const usize maxChunks = DivideUp(count, effectiveGrainSize);
        const usize totalThreads = static_cast<usize>(m_threadCount) + 1;
        runParallelForChunks(begin, end, count, computeChunkCount(maxChunks, totalThreads), func);
    }

public:
    inline void drain()noexcept{
        if(isExecutingOnCurrentThread()){
            NWB_FATAL_ASSERT_MSG(false, NWB_TEXT("ThreadPool task execution cannot drain or destroy its own task domain"));
            TerminateInvariant();
        }
        waitTaskDomain();
    }

    inline void finish(){
        if(isExecutingOnCurrentThread())
            throw RuntimeException("ThreadPool task execution cannot finish its own task domain");
        waitTaskDomain();
    }

    inline void wait(){
        if(isExecutingOnCurrentThread())
            throw RuntimeException("ThreadPool task execution cannot wait for its own task domain");
        waitTaskDomain();
    }

public:
    [[nodiscard]] inline u64 domainIdentity()const noexcept{ return m_domainIdentity; }
    [[nodiscard]] inline u32 workerThreadCount()const{ return m_threadCount; }
    inline bool isParallelEnabled()const{ return m_threadCount > 0; }
    // The caller thread is logical worker zero. Worker threads receive stable nonzero IDs for the lifetime of this
    // pool, so native packet recorders can lease worker-affined command storage without depending on OS thread IDs.
    [[nodiscard]] inline usize currentWorkerIndex()const noexcept{
        return s_CurrentWorkerPool == this ? s_CurrentWorkerIndex : 0u;
    }


private:
    inline bool hasParallelWork()const{
        ParallelForDesc* pf = m_pfWork.load(MemoryOrder::acquire);
        return pf && pf->nextChunk.load(MemoryOrder::relaxed) < pf->numChunks;
    }

    template<typename Func>
    inline void dispatchParallelFor(usize begin, usize, const Func& func, usize numChunks, usize chunkSize, usize remainder){
        Latch done(static_cast<isize>(numChunks));
        ScopedLock parallelLock(m_pfMutex);

        ParallelForDesc desc;
        desc.invoke = [](const void* ctx, usize cb, usize ce){
            const auto& f = *static_cast<const Func*>(ctx);
            for(usize i = cb; i < ce; ++i)
                f(i);
        };
        desc.functor = &func;
        desc.owner = this;
        desc.parentExecution = s_CurrentParallelForExecution;
        desc.nextChunk.store(0, MemoryOrder::relaxed);
        desc.numChunks = numChunks;
        desc.begin = begin;
        desc.chunkSize = chunkSize;
        desc.remainder = remainder;
        desc.done = &done;

        {
            ScopedLock taskLock(m_taskMutex);
            NWB_ASSERT(m_activeParallelWorkers.load(MemoryOrder::relaxed) == 0);
            m_pfWork.store(&desc, MemoryOrder::release);
        }
        bool completed = false;
        ScopeExit retireDispatch([&]()noexcept{
            if(!completed){
                desc.cancelRequested.store(true, MemoryOrder::release);
                const usize firstUnclaimed = desc.nextChunk.exchange(desc.numChunks, MemoryOrder::acq_rel);
                if(firstUnclaimed < desc.numChunks)
                    done.count_down(static_cast<isize>(desc.numChunks - firstUnclaimed));
            }
            done.wait();
            {
                ScopedLock taskLock(m_taskMutex);
                m_pfWork.store(nullptr, MemoryOrder::release);
            }
            usize activeWorkers = m_activeParallelWorkers.load(MemoryOrder::acquire);
            while(activeWorkers > 0u){
                m_activeParallelWorkers.wait(activeWorkers, MemoryOrder::relaxed);
                activeWorkers = m_activeParallelWorkers.load(MemoryOrder::acquire);
            }
        });
        m_taskAvailable.notify_all();
        processParallelFor(&desc);
        done.wait();
        completed = true;
    }

    inline void workerLoop(const StopToken& stopToken, u64 affinityMask, const usize workerIndex){
        ScopedWorkerExecution workerExecution(this, workerIndex);
        SetCurrentThreadCpuAffinity(affinityMask);

        for(;;){
            TaskItem* item = nullptr;
            ParallelForDesc* pf = nullptr;

            {
                UniqueLock taskLock(m_taskMutex);
                if(!m_taskAvailable.wait(taskLock, stopToken, [this](){
                        return hasParallelWork() || !m_tasks.empty();
                    })
                )
                    break;

                const bool parallelWorkAvailable = hasParallelWork();
                if(parallelWorkAvailable){
                    pf = m_pfWork.load(MemoryOrder::acquire);
                    if(pf)
                        m_activeParallelWorkers.fetch_add(1u, MemoryOrder::acq_rel);
                }
                else if(!m_tasks.empty()){
                    item = m_tasks.front();
                    m_tasks.pop_front();
                    ++m_activeTaskWorkers;
                }
            }

            if(item){
                ScopeExit retireTask([&]()noexcept{
                    resetTaskCaptures(item);
                    bool taskDomainQuiesced;
                    {
                        ScopedLock taskLock(m_taskMutex);
                        deallocateTaskNodesLocked(item);
                        NWB_ASSERT(m_pendingCount > 0u);
                        NWB_ASSERT(m_activeTaskWorkers > 0u);
                        --m_pendingCount;
                        --m_activeTaskWorkers;
                        taskDomainQuiesced = m_pendingCount == 0u && m_activeTaskWorkers == 0u;
                    }
                    if(taskDomainQuiesced)
                        m_taskStateChanged.notify_all();
                });
                item->func();
            }
            else if(pf){
                ScopeExit retireWorker([&]()noexcept{
                    if(m_activeParallelWorkers.fetch_sub(1u, MemoryOrder::acq_rel) == 1u)
                        m_activeParallelWorkers.notify_all();
                });
                processParallelFor(pf);
            }
        }
    }


private:
    inline static thread_local const ScopedParallelForExecution* s_CurrentParallelForExecution = nullptr;
    inline static thread_local ThreadPool* s_CurrentWorkerPool = nullptr;
    inline static thread_local usize s_CurrentWorkerIndex = 0u;

    u64 m_domainIdentity;
    Atomic<ParallelForDesc*> m_pfWork{ nullptr };
    PersistentArena m_arena;
    TaskQueue m_tasks;
    Futex m_taskMutex;
    Futex m_pfMutex;
    ConditionVariableAny m_taskAvailable;
    ConditionVariableAny m_taskStateChanged;
    usize m_pendingCount = 0u;
    usize m_activeTaskWorkers = 0u;
    Atomic<usize> m_activeParallelWorkers{ 0 };
    u32 m_threadCount;
    WorkerList m_workers;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ITaskScheduler{
public:
    inline explicit ITaskScheduler(ThreadPool& pool)
        : m_taskPool(pool)
    {}


public:
    inline ThreadPool& taskPool(){ return m_taskPool; }
    inline const ThreadPool& taskPool()const{ return m_taskPool; }


protected:
    template<typename Func>
    inline void scheduleParallelFor(usize begin, usize end, const Func& func){
        m_taskPool.parallelFor(begin, end, func);
    }

    template<typename Func>
    inline void scheduleParallelFor(usize begin, usize end, usize grainSize, const Func& func){
        m_taskPool.parallelFor(begin, end, grainSize, func);
    }

    inline void waitTasks(){ m_taskPool.wait(); }


private:
    ThreadPool& m_taskPool;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

