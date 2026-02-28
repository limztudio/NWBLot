// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "memory.h"
#include "arena_object.h"
#include "thread.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class JobSystem : NoCopy{
public:
    struct JobHandle{
        static constexpr u32 s_InvalidIndex = static_cast<u32>(-1);

        u32 index = s_InvalidIndex;
        u32 generation = 0;

        inline bool isValid()const{ return index != s_InvalidIndex && generation != 0; }
        inline explicit operator bool()const{ return isValid(); }
    };


private:
    static constexpr usize s_JobInlineStorageBytes = 384;
    static constexpr u32 s_WorkFirstDepthLimit = 8;
    static constexpr u32 s_ReadyBatchCapacity = 256;


private:
    using JobFunction = InplaceFunction<s_JobInlineStorageBytes>;
    using ReadyBatch = FixedVector<JobHandle, s_ReadyBatchCapacity>;


private:
    struct JobSignal{
        Atomic<u32> completedGeneration{ 0 };
    };

    struct JobNode{
        using DependencyAllocator = MemoryAllocator<JobHandle>;

        JobFunction func;
        Vector<JobHandle, DependencyAllocator> dependents;
        JobSignal* completionSignal = nullptr;
        u32 generation = 1;
        u32 remainingDependencies = 0;
        bool completed = true;
        bool scheduled = false;


    public:
        inline explicit JobNode(MemoryArena& arena)
            : dependents(DependencyAllocator(arena))
        {}
    };


private:
    using JobNodeAllocator = MemoryAllocator<JobNode>;
    using JobFreeNodeAllocator = MemoryAllocator<u32>;


private:
    static constexpr usize s_DefaultMinimumArenaSize = 65536;
    static constexpr usize s_DefaultNodeReservePerThread = 256;


private:
    static inline usize defaultNodeReserveCount(u32 threadCount){
        const usize totalThreads = static_cast<usize>(threadCount) + 1;
        return totalThreads * s_DefaultNodeReservePerThread;
    }

    static inline usize defaultArenaSize(u32 threadCount){
        const usize reserveCount = defaultNodeReserveCount(threadCount);
        const usize nodeBytes = reserveCount * sizeof(JobNode);
        const usize dependencyBytes = reserveCount * 4 * sizeof(JobHandle);
        const usize freeListBytes = reserveCount * sizeof(u32);
        const usize total = nodeBytes + dependencyBytes + freeListBytes + 4096;
        const usize arenaSize = total > s_DefaultMinimumArenaSize ? total : s_DefaultMinimumArenaSize;
        return MemoryArena::StructureAlignedSize(arenaSize);
    }

    static inline usize resolveArenaSize(u32 threadCount, usize arenaSize){
        if(arenaSize > 0)
            return arenaSize;

        return defaultArenaSize(threadCount);
    }


public:
    inline explicit JobSystem(ThreadPool& pool, usize arenaSize = 0)
        : m_pool(pool)
        , m_arena(resolveArenaSize(pool.m_threadCount, arenaSize))
        , m_nodes(JobNodeAllocator(m_arena))
        , m_freeNodes(JobFreeNodeAllocator(m_arena))
    {
        const usize reserveCount = defaultNodeReserveCount(pool.m_threadCount);
        m_nodes.reserve(reserveCount);
        m_freeNodes.reserve(reserveCount);
    }
    inline explicit JobSystem(u32 threadCount, u64 affinityMask = 0, usize arenaSize = 0)
        : m_ownedPool(MakeUnique<ThreadPool>(threadCount, affinityMask, arenaSize))
        , m_pool(*m_ownedPool)
        , m_arena(resolveArenaSize(threadCount, arenaSize))
        , m_nodes(JobNodeAllocator(m_arena))
        , m_freeNodes(JobFreeNodeAllocator(m_arena))
    {
        const usize reserveCount = defaultNodeReserveCount(threadCount);
        m_nodes.reserve(reserveCount);
        m_freeNodes.reserve(reserveCount);
    }
    inline explicit JobSystem(u32 threadCount, CoreAffinity affinity, usize arenaSize = 0)
        : m_ownedPool(MakeUnique<ThreadPool>(threadCount, affinity, arenaSize))
        , m_pool(*m_ownedPool)
        , m_arena(resolveArenaSize(threadCount, arenaSize))
        , m_nodes(JobNodeAllocator(m_arena))
        , m_freeNodes(JobFreeNodeAllocator(m_arena))
    {
        const usize reserveCount = defaultNodeReserveCount(threadCount);
        m_nodes.reserve(reserveCount);
        m_freeNodes.reserve(reserveCount);
    }

    inline ~JobSystem(){
        waitAll();
    }


public:
    template<typename Func>
    inline JobHandle submit(Func&& task){
        return submitWithDependencies(JobFunction(Forward<Func>(task)), nullptr, 0);
    }

    template<typename Func>
    inline JobHandle submit(Func&& task, JobHandle dependency){
        return submitWithDependencies(JobFunction(Forward<Func>(task)), &dependency, 1);
    }

    template<typename Func>
    inline JobHandle submit(Func&& task, std::initializer_list<JobHandle> dependencies){
        return submitWithDependencies(JobFunction(Forward<Func>(task)), dependencies.begin(), dependencies.size());
    }

    template<typename Func>
    inline JobHandle submit(Func&& task, const JobHandle* dependencies, usize dependencyCount){
        return submitWithDependencies(JobFunction(Forward<Func>(task)), dependencies, dependencyCount);
    }

    template<typename Func>
    inline JobHandle then(JobHandle dependency, Func&& task){
        return submit(Forward<Func>(task), dependency);
    }

public:
    inline void wait(JobHandle handle){
        if(!handle.isValid())
            return;

        for(;;){
            JobSignal* completionSignal = nullptr;
            u32 completedGeneration = 0;

            {
                ScopedLock lock(m_mutex);

                JobNode* node = tryResolveNodeLocked(handle);
                if(!node)
                    return;

                completionSignal = node->completionSignal;
                NWB_ASSERT_MSG(completionSignal != nullptr, NWB_TEXT("JobSystem encountered a null completion signal"));

                completedGeneration = completionSignal->completedGeneration.load(std::memory_order_acquire);
                if(completedGeneration >= handle.generation)
                    return;
            }

            completionSignal->completedGeneration.wait(completedGeneration, std::memory_order_relaxed);
        }
    }

    inline void wait(std::initializer_list<JobHandle> handles){
        for(const JobHandle handle : handles)
            wait(handle);
    }

    inline void waitAll(){
        i32 current;
        while((current = m_pendingJobCount.load(std::memory_order_acquire)) > 0)
            m_pendingJobCount.wait(current, std::memory_order_relaxed);
    }

    inline bool isComplete(JobHandle handle)const{
        if(!handle.isValid())
            return true;

        ScopedLock lock(m_mutex);
        return !isPendingLocked(handle);
    }

private:
    inline JobHandle submitWithDependencies(JobFunction&& task, const JobHandle* dependencies, usize dependencyCount){
        if(!task)
            return JobHandle{};

        JobHandle output;
        bool shouldSchedule = false;

        {
            ScopedLock lock(m_mutex);

            output = acquireNodeLocked(Move(task));
            JobNode* node = tryResolveNodeLocked(output);
            NWB_ASSERT_MSG(node != nullptr, NWB_TEXT("JobSystem created an invalid job node"));

            u32 unresolved = 0;
            for(usize i = 0; i < dependencyCount; ++i){
                const JobHandle dependency = dependencies[i];
                JobNode* dependencyNode = tryResolveNodeLocked(dependency);
                if(!dependencyNode)
                    continue;

                dependencyNode->dependents.push_back(output);
                ++unresolved;
            }

            node->remainingDependencies = unresolved;
            if(unresolved == 0){
                node->scheduled = true;
                shouldSchedule = true;
            }
        }

        m_pendingJobCount.fetch_add(1, std::memory_order_release);

        if(shouldSchedule)
            enqueueExecution(output);

        return output;
    }

    inline JobHandle acquireNodeLocked(JobFunction&& task){
        u32 index = JobHandle::s_InvalidIndex;
        if(!m_freeNodes.empty()){
            index = m_freeNodes.back();
            m_freeNodes.pop_back();
        }
        else{
            NWB_ASSERT_MSG(m_nodes.size() < static_cast<usize>(JobHandle::s_InvalidIndex), NWB_TEXT("JobSystem exceeded maximum number of trackable jobs"));
            index = static_cast<u32>(m_nodes.size());
            m_nodes.emplace_back(m_arena);

            JobNode& createdNode = m_nodes[index];
            createdNode.completionSignal = NewArenaObject<JobSignal>(m_arena);
            NWB_ASSERT_MSG(createdNode.completionSignal != nullptr, NWB_TEXT("JobSystem failed to allocate a completion signal"));
        }

        JobNode& node = m_nodes[index];
        NWB_ASSERT_MSG(node.completionSignal != nullptr, NWB_TEXT("JobSystem acquired an invalid completion signal"));
        node.func = Move(task);
        node.dependents.clear();
        node.remainingDependencies = 0;
        node.completed = false;
        node.scheduled = false;

        JobHandle output;
        output.index = index;
        output.generation = node.generation;
        return output;
    }

    inline void recycleNodeLocked(u32 index, JobNode& node){
        node.func = JobFunction();
        node.dependents.clear();
        node.remainingDependencies = 0;
        node.completed = true;
        node.scheduled = false;

        ++node.generation;
        if(node.generation == 0)
            node.generation = 1;

        m_freeNodes.push_back(index);
    }

    inline bool isPendingLocked(JobHandle handle)const{
        if(handle.index >= m_nodes.size())
            return false;

        const JobNode& node = m_nodes[handle.index];
        if(node.generation != handle.generation)
            return false;

        return !node.completed;
    }

    inline JobNode* tryResolveNodeLocked(JobHandle handle){
        if(!handle.isValid() || handle.index >= m_nodes.size())
            return nullptr;

        JobNode& node = m_nodes[handle.index];
        if(node.generation != handle.generation || node.completed)
            return nullptr;

        return &node;
    }

    inline void enqueueExecution(JobHandle handle){
        m_pool.enqueue([this, handle](){
            execute(handle);
        });
    }

    inline void enqueueExecutionBatch(const JobHandle* handles, usize handleCount){
        if(handleCount == 0)
            return;

        m_pool.enqueueBatch(handleCount, [this, handles](usize i){
            const JobHandle handle = handles[i];
            return [this, handle](){
                execute(handle);
            };
        });
    }

    inline void execute(JobHandle handle){
        JobHandle current = handle;
        u32 workFirstDepth = 0;

        while(current.isValid()){
            JobFunction task;
            {
                ScopedLock lock(m_mutex);

                JobNode* node = tryResolveNodeLocked(current);
                if(!node || !node->scheduled)
                    return;

                task = Move(node->func);
            }

            if(task)
                task();

            const bool allowInline = workFirstDepth < s_WorkFirstDepthLimit;
            const JobHandle inlineContinuation = complete(current, allowInline);
            if(!inlineContinuation.isValid())
                return;

            current = inlineContinuation;
            ++workFirstDepth;
        }
    }

    inline JobHandle complete(JobHandle handle, bool allowInline){
        ReadyBatch readyJobs;
        JobSignal* completionSignal = nullptr;
        JobHandle inlineContinuation;

        {
            ScopedLock lock(m_mutex);

            JobNode* node = tryResolveNodeLocked(handle);
            if(!node)
                return JobHandle{};

            completionSignal = node->completionSignal;
            NWB_ASSERT_MSG(completionSignal != nullptr, NWB_TEXT("JobSystem encountered a null completion signal"));

            for(usize i = 0; i < node->dependents.size(); ++i){
                const JobHandle dependentHandle = node->dependents[i];
                JobNode* dependentNode = tryResolveNodeLocked(dependentHandle);
                if(!dependentNode)
                    continue;

                NWB_ASSERT_MSG(dependentNode->remainingDependencies > 0, NWB_TEXT("JobSystem dependency counter underflow"));
                if(dependentNode->remainingDependencies == 0)
                    continue;

                --dependentNode->remainingDependencies;
                if(dependentNode->remainingDependencies == 0 && !dependentNode->scheduled){
                    dependentNode->scheduled = true;

                    if(allowInline && !inlineContinuation.isValid()){
                        inlineContinuation = dependentHandle;
                        continue;
                    }

                    NWB_ASSERT_MSG(readyJobs.size() < readyJobs.max_size(), NWB_TEXT("JobSystem ready batch capacity exceeded"));
                    readyJobs.push_back(dependentHandle);
                }
            }

            recycleNodeLocked(handle.index, *node);
        }

        completionSignal->completedGeneration.store(handle.generation, std::memory_order_release);
        completionSignal->completedGeneration.notify_all();

        enqueueExecutionBatch(readyJobs.data(), readyJobs.size());

        if(m_pendingJobCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
            m_pendingJobCount.notify_all();

        return inlineContinuation;
    }


private:
    UniquePtr<ThreadPool> m_ownedPool;
    ThreadPool& m_pool;

    MemoryArena m_arena;
    Vector<JobNode, JobNodeAllocator> m_nodes;
    Vector<u32, JobFreeNodeAllocator> m_freeNodes;

    mutable Futex m_mutex;
    Atomic<i32> m_pendingJobCount{ 0 };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IJobScheduler{
public:
    inline explicit IJobScheduler(JobSystem& jobSystem) : m_jobSystem(jobSystem){}


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
    inline JobSystem::JobHandle scheduleJob(Func&& task, std::initializer_list<JobSystem::JobHandle> dependencies){
        return m_jobSystem.submit(Forward<Func>(task), dependencies);
    }

    inline void waitJob(JobSystem::JobHandle handle){
        m_jobSystem.wait(handle);
    }

    inline void waitJobs(std::initializer_list<JobSystem::JobHandle> handles){
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

