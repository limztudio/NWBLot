// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


extern u64 QueryAffinityMask(CoreAffinity type);
extern u32 QueryCoreCount(CoreAffinity type);
extern void SetCurrentThreadAffinity(u64 mask);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ThreadPool : NoCopy{
private:
    struct TaskItem{
        Function<void()> func;
        bool tracked;
    };

    using WorkerAllocator = MemoryAllocator<JoiningThread>;
    using TaskAllocator = MemoryAllocator<TaskItem>;


private:
    static inline usize defaultArenaSize(u32 threadCount){
        const usize workerBytes = static_cast<usize>(threadCount + 1) * sizeof(JoiningThread);
        const usize taskBytes = static_cast<usize>(threadCount) * 8 * sizeof(TaskItem);
        const usize total = workerBytes + taskBytes + 4096;
        return MemoryArena::StructureAlignedSize(total > 32768 ? total : 32768);
    }


public:
    inline explicit ThreadPool(u32 threadCount, u64 affinityMask = 0, usize arenaSize = 0)
        : m_arena(arenaSize > 0 ? arenaSize : defaultArenaSize(threadCount))
        , m_workers(WorkerAllocator(m_arena))
        , m_tasks(TaskAllocator(m_arena))
        , m_threadCount(threadCount)
    {
        m_workers.reserve(threadCount);
        for(u32 i = 0; i < threadCount; ++i){
            m_workers.emplace_back([this, affinityMask](StopToken st){
                workerLoop(st, affinityMask);
            });
        }
    }
    inline explicit ThreadPool(u32 threadCount, CoreAffinity affinity, usize arenaSize = 0)
        : ThreadPool(threadCount, QueryAffinityMask(affinity), arenaSize)
    {}

    inline ~ThreadPool(){
        waitPending();
    }


public:
    template<typename Func>
    inline void enqueue(Func&& task){
        if(m_threadCount == 0){
            Forward<Func>(task)();
            return;
        }
        m_pendingCount.fetch_add(1, std::memory_order_release);
        {
            LockGuard lock(m_mutex);
            m_tasks.push_back(TaskItem{ Function<void()>(Forward<Func>(task)), true });
        }
        m_taskAvailable.notify_one();
    }

    template<typename Func, typename Callback>
    inline void enqueue(Func&& task, Callback&& onComplete){
        enqueue([t = Function<void()>(Forward<Func>(task)),
                 cb = Function<void()>(Forward<Callback>(onComplete))](){
            t();
            cb();
        });
    }

public:
    template<typename Func>
    inline void parallelFor(usize begin, usize end, const Func& func){
        if(begin >= end)
            return;

        const usize count = end - begin;

        if(m_threadCount == 0 || count == 1){
            for(usize i = begin; i < end; ++i)
                func(i);
            return;
        }

        const usize totalThreads = static_cast<usize>(m_threadCount) + 1;
        const usize numChunks = (count < totalThreads) ? count : totalThreads;
        const usize workerChunks = numChunks - 1;

        if(workerChunks == 0){
            for(usize i = begin; i < end; ++i)
                func(i);
            return;
        }

        const usize chunkSize = count / numChunks;
        const usize remainder = count % numChunks;

        Latch done(static_cast<std::ptrdiff_t>(workerChunks));

        usize pos = begin;
        for(usize t = 0; t < workerChunks; ++t){
            usize thisChunk = chunkSize + ((t < remainder) ? 1 : 0);
            usize chunkBegin = pos;
            usize chunkEnd = pos + thisChunk;
            pos = chunkEnd;

            {
                LockGuard lock(m_mutex);
                m_tasks.push_back(TaskItem{
                    [&func, chunkBegin, chunkEnd, &done](){
                        for(usize i = chunkBegin; i < chunkEnd; ++i)
                            func(i);
                        done.count_down();
                    },
                    false
                });
            }
            m_taskAvailable.notify_one();
        }

        for(usize i = pos; i < end; ++i)
            func(i);

        done.wait();
    }

    template<typename Func>
    inline void parallelFor(usize begin, usize end, usize grainSize, const Func& func){
        if(begin >= end)
            return;

        const usize count = end - begin;

        if(m_threadCount == 0 || count <= grainSize){
            for(usize i = begin; i < end; ++i)
                func(i);
            return;
        }

        const usize maxChunks = (count + grainSize - 1) / grainSize;
        const usize totalThreads = static_cast<usize>(m_threadCount) + 1;
        const usize numChunks = (maxChunks < totalThreads) ? maxChunks : totalThreads;
        const usize workerChunks = numChunks - 1;

        if(workerChunks == 0){
            for(usize i = begin; i < end; ++i)
                func(i);
            return;
        }

        const usize chunkSize = count / numChunks;
        const usize remainder = count % numChunks;

        Latch done(static_cast<std::ptrdiff_t>(workerChunks));

        usize pos = begin;
        for(usize t = 0; t < workerChunks; ++t){
            usize thisChunk = chunkSize + ((t < remainder) ? 1 : 0);
            usize chunkBegin = pos;
            usize chunkEnd = pos + thisChunk;
            pos = chunkEnd;

            {
                LockGuard lock(m_mutex);
                m_tasks.push_back(TaskItem{
                    [&func, chunkBegin, chunkEnd, &done](){
                        for(usize i = chunkBegin; i < chunkEnd; ++i)
                            func(i);
                        done.count_down();
                    },
                    false
                });
            }
            m_taskAvailable.notify_one();
        }

        for(usize i = pos; i < end; ++i)
            func(i);

        done.wait();
    }

public:
    inline void wait(){ waitPending(); }

public:
    inline u32 threadCount()const{ return m_threadCount; }


private:
    inline void waitPending(){
        i32 current;
        while((current = m_pendingCount.load(std::memory_order_acquire)) > 0)
            m_pendingCount.wait(current, std::memory_order_relaxed);
    }

    inline void workerLoop(StopToken stopToken, u64 affinityMask){
        SetCurrentThreadAffinity(affinityMask);

        while(true){
            TaskItem item;
            {
                UniqueLock lock(m_mutex);
                if(!m_taskAvailable.wait(lock, stopToken, [this](){ return !m_tasks.empty(); }))
                    break;

                item = Move(m_tasks.front());
                m_tasks.pop_front();
            }

            item.func();

            if(item.tracked){
                if(m_pendingCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
                    m_pendingCount.notify_all();
            }
        }
    }


private:
    MemoryArena m_arena;
    Vector<JoiningThread, WorkerAllocator> m_workers;
    Deque<TaskItem, TaskAllocator> m_tasks;
    Mutex m_mutex;
    ConditionVariableAny m_taskAvailable;
    Atomic<i32> m_pendingCount{ 0 };
    u32 m_threadCount;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ITaskScheduler{
public:
    inline explicit ITaskScheduler(ThreadPool& pool) : m_taskPool(pool){}


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
