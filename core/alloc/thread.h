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


template<usize InlineStorageSize>
class InplaceFunction : NoCopy{
private:
    struct Storage{
        alignas(MaxAlign) u8 data[InlineStorageSize];
    };

    using InvokeFn = void(*)(void*);
    using DestroyFn = void(*)(void*);
    using MoveFn = void(*)(void*, void*);


public:
    inline InplaceFunction() = default;
    inline ~InplaceFunction(){ reset(); }

    inline InplaceFunction(InplaceFunction&& rhs)noexcept{
        moveFrom(Move(rhs));
    }

    inline InplaceFunction& operator=(InplaceFunction&& rhs)noexcept{
        if(this != &rhs){
            reset();
            moveFrom(Move(rhs));
        }
        return *this;
    }

    template<typename Func, typename = EnableIf_T<!IsSame_V<Decay_T<Func>, InplaceFunction>>>
    inline explicit InplaceFunction(Func&& func){
        assign(Forward<Func>(func));
    }

    template<typename Func, typename = EnableIf_T<!IsSame_V<Decay_T<Func>, InplaceFunction>>>
    inline InplaceFunction& operator=(Func&& func){
        reset();
        assign(Forward<Func>(func));
        return *this;
    }

    inline explicit operator bool()const{ return m_invoke != nullptr; }

    inline void operator()(){
        NWB_ASSERT_MSG(m_invoke != nullptr, NWB_TEXT("InplaceFunction invoked without target"));
        m_invoke(storagePtr());
    }

    inline void reset(){
        if(!m_destroy)
            return;

        m_destroy(storagePtr());
        m_invoke = nullptr;
        m_destroy = nullptr;
        m_move = nullptr;
    }


private:
    template<typename Func>
    inline void assign(Func&& func){
        using FuncType = Decay_T<Func>;

        static_assert(sizeof(FuncType) <= InlineStorageSize, "InplaceFunction capture size exceeds inline storage");
        static_assert(alignof(FuncType) <= alignof(Storage), "InplaceFunction capture alignment exceeds inline storage");

        new(storagePtr()) FuncType(Forward<Func>(func));

        m_invoke = [](void* storage){
            FuncType* f = static_cast<FuncType*>(storage);
            (*f)();
        };
        m_destroy = [](void* storage){
            FuncType* f = static_cast<FuncType*>(storage);
            f->~FuncType();
        };
        m_move = [](void* dst, void* src){
            FuncType* source = static_cast<FuncType*>(src);
            new(dst) FuncType(Move(*source));
            source->~FuncType();
        };
    }

    inline void moveFrom(InplaceFunction&& rhs)noexcept{
        if(!rhs.m_invoke)
            return;

        rhs.m_move(storagePtr(), rhs.storagePtr());
        m_invoke = rhs.m_invoke;
        m_destroy = rhs.m_destroy;
        m_move = rhs.m_move;

        rhs.m_invoke = nullptr;
        rhs.m_destroy = nullptr;
        rhs.m_move = nullptr;
    }

    inline void* storagePtr(){
        return static_cast<void*>(m_storage.data);
    }

    inline void* storagePtr()const{
        return const_cast<void*>(static_cast<const void*>(m_storage.data));
    }


private:
    Storage m_storage = {};
    InvokeFn m_invoke = nullptr;
    DestroyFn m_destroy = nullptr;
    MoveFn m_move = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class JobSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ThreadPool : NoCopy{
    friend class JobSystem;


private:
    static constexpr usize s_TaskInlineStorageBytes = 128;
    static constexpr usize s_ChunkOversubscription = 4;


private:
    using TaskFunction = InplaceFunction<s_TaskInlineStorageBytes>;


private:
    struct TaskItem{
        TaskFunction func;
    };

    struct ParallelForDesc{
        void (*invoke)(const void* functor, usize chunkBegin, usize chunkEnd);
        const void* functor;
        Atomic<usize> nextChunk{ 0 };
        usize numChunks;
        usize begin;
        usize chunkSize;
        usize remainder;
        Latch* done;
        Atomic<i32> activeWorkers{ 0 };
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

    static inline usize computeChunkCount(usize maxChunks, usize totalThreads){
        usize targetChunks = totalThreads;
        if(totalThreads <= static_cast<usize>(-1) / s_ChunkOversubscription)
            targetChunks = totalThreads * s_ChunkOversubscription;

        return (maxChunks < targetChunks) ? maxChunks : targetChunks;
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
            ScopedLock lock(m_taskMutex);
            m_tasks.push_back(TaskItem{ TaskFunction(Forward<Func>(task)) });
        }
        m_taskAvailable.notify_one();
    }

    template<typename TaskBuilder>
    inline void enqueueBatch(usize taskCount, const TaskBuilder& taskBuilder){
        if(taskCount == 0)
            return;

        NWB_ASSERT_MSG(m_threadCount > 0, NWB_TEXT("enqueueBatch requires at least one worker thread"));

        m_pendingCount.fetch_add(static_cast<i32>(taskCount), std::memory_order_release);
        {
            ScopedLock lock(m_taskMutex);
            for(usize i = 0; i < taskCount; ++i)
                m_tasks.push_back(TaskItem{ TaskFunction(taskBuilder(i)) });
        }

        const usize wakeCount = Min(taskCount, static_cast<usize>(m_threadCount));
        for(usize i = 0; i < wakeCount; ++i)
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

        if(m_threadCount == 0 || count == 1){
            for(usize i = begin; i < end; ++i)
                func(i);
            return;
        }

        const usize totalThreads = static_cast<usize>(m_threadCount) + 1;
        const usize numChunks = computeChunkCount(count, totalThreads);

        if(numChunks <= 1){
            for(usize i = begin; i < end; ++i)
                func(i);
            return;
        }

        const usize chunkSize = count / numChunks;
        const usize remainder = count % numChunks;

        dispatchParallelFor(begin, end, func, numChunks, chunkSize, remainder);
    }

    template<typename Func>
    inline void parallelFor(usize begin, usize end, usize grainSize, const Func& func){
        if(begin >= end)
            return;

        const usize count = end - begin;
        const usize effectiveGrainSize = grainSize > 0 ? grainSize : 1;

        if(m_threadCount == 0 || count <= effectiveGrainSize){
            for(usize i = begin; i < end; ++i)
                func(i);
            return;
        }

        const usize maxChunks = (count + effectiveGrainSize - 1) / effectiveGrainSize;
        const usize totalThreads = static_cast<usize>(m_threadCount) + 1;
        const usize numChunks = computeChunkCount(maxChunks, totalThreads);

        if(numChunks <= 1){
            for(usize i = begin; i < end; ++i)
                func(i);
            return;
        }

        const usize chunkSize = count / numChunks;
        const usize remainder = count % numChunks;

        dispatchParallelFor(begin, end, func, numChunks, chunkSize, remainder);
    }

public:
    inline void wait(){ waitPending(); }

public:
    inline bool isParallelEnabled()const{ return m_threadCount > 0; }


private:
    inline void waitPending(){
        i32 current;
        while((current = m_pendingCount.load(std::memory_order_acquire)) > 0)
            m_pendingCount.wait(current, std::memory_order_relaxed);
    }

    inline bool hasParallelWork()const{
        ParallelForDesc* pf = m_pfWork.load(std::memory_order_acquire);
        return pf && pf->nextChunk.load(std::memory_order_relaxed) < pf->numChunks;
    }

    static inline void processParallelFor(ParallelForDesc* pf){
        while(true){
            const usize c = pf->nextChunk.fetch_add(1, std::memory_order_relaxed);
            if(c >= pf->numChunks)
                break;

            const usize cb = pf->begin + c * pf->chunkSize + ((c < pf->remainder) ? c : pf->remainder);
            const usize ce = cb + pf->chunkSize + ((c < pf->remainder) ? 1 : 0);

            pf->invoke(pf->functor, cb, ce);
            pf->done->count_down();
        }
    }

    template<typename Func>
    inline void dispatchParallelFor(usize begin, usize, const Func& func, usize numChunks, usize chunkSize, usize remainder){
        Latch done(static_cast<std::ptrdiff_t>(numChunks));
        UniqueLock parallelLock(m_pfMutex);

        ParallelForDesc desc;
        desc.invoke = [](const void* ctx, usize cb, usize ce){
            const auto& f = *static_cast<const Func*>(ctx);
            for(usize i = cb; i < ce; ++i)
                f(i);
        };
        desc.functor = &func;
        desc.nextChunk.store(0, std::memory_order_relaxed);
        desc.numChunks = numChunks;
        desc.begin = begin;
        desc.chunkSize = chunkSize;
        desc.remainder = remainder;
        desc.done = &done;
        desc.activeWorkers.store(0, std::memory_order_relaxed);

        m_pfWork.store(&desc, std::memory_order_release);
        m_taskAvailable.notify_all();

        processParallelFor(&desc);

        done.wait();

        i32 activeWorkers = 0;
        while((activeWorkers = desc.activeWorkers.load(std::memory_order_acquire)) > 0)
            desc.activeWorkers.wait(activeWorkers, std::memory_order_relaxed);

        m_pfWork.store(nullptr, std::memory_order_release);
    }

    inline void workerLoop(StopToken stopToken, u64 affinityMask){
        SetCurrentThreadAffinity(affinityMask);

        for(;;){
            TaskItem item;
            bool hasTask = false;

            {
                UniqueLock taskLock(m_taskMutex);
                if(!m_taskAvailable.wait(taskLock, stopToken, [this](){
                    return hasParallelWork() || !m_tasks.empty();
                }))
                    break;

                const bool parallelWorkAvailable = hasParallelWork();
                if(!parallelWorkAvailable && !m_tasks.empty()){
                    item = Move(m_tasks.front());
                    m_tasks.pop_front();
                    hasTask = true;
                }
            }

            if(hasTask){
                item.func();

                if(m_pendingCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
                    m_pendingCount.notify_all();
            }
            else{
                ParallelForDesc* pf = m_pfWork.load(std::memory_order_acquire);
                if(!pf)
                    continue;

                pf->activeWorkers.fetch_add(1, std::memory_order_acq_rel);
                processParallelFor(pf);
                if(pf->activeWorkers.fetch_sub(1, std::memory_order_acq_rel) == 1)
                    pf->activeWorkers.notify_all();
            }
        }
    }


private:
    MemoryArena m_arena;
    Vector<JoiningThread, WorkerAllocator> m_workers;
    Deque<TaskItem, TaskAllocator> m_tasks;
    Atomic<ParallelForDesc*> m_pfWork{ nullptr };
    Futex m_taskMutex;
    Futex m_pfMutex;
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

