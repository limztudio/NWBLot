// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "persistent.h"
#include "scratch.h"

#include <global/cpu_topology.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CpuTaskCost{
    enum Enum : u8{
        Any,
        Heavy,
        Light
    };
};

namespace CpuTaskPriority{
    enum Enum : u8{
        Critical,
        Normal,
        Background
    };
};

namespace CpuTaskTarget{
    enum Enum : u8{
        Worker,
        MainThread
    };
};


struct CpuTaskOptions{
    CpuTaskCost::Enum cost = CpuTaskCost::Heavy;
    CpuTaskPriority::Enum priority = CpuTaskPriority::Normal;
    CpuTaskTarget::Enum target = CpuTaskTarget::Worker;
};

struct CpuTaskSchedulerConfig{
    static constexpr u32 s_AutomaticWorkerCount = Limit<u32>::s_Max;

    u32 workerCount = s_AutomaticWorkerCount;
    u32 reservedThreadCount = 1u;
    bool heterogeneous = true;
};

struct CpuTaskHandle{
    static constexpr u32 s_InvalidIndex = Limit<u32>::s_Max;

    u64 domainIdentity = 0u;
    u32 index = s_InvalidIndex;
    u32 generation = 0u;

    [[nodiscard]] bool valid()const noexcept{ return domainIdentity != 0u && index != s_InvalidIndex && generation != 0u; }
    explicit operator bool()const noexcept{ return valid(); }
};

struct CpuTaskSchedulerStatistics{
    u64 completedTasks = 0u;
    u64 canceledTasks = 0u;
    u64 performanceTasks = 0u;
    u64 efficiencyTasks = 0u;
    u64 unclassifiedTasks = 0u;
    u64 cooperativeTasks = 0u;
    usize outstandingTasks = 0u;
    usize peakOutstandingTasks = 0u;
    u32 performanceWorkers = 0u;
    u32 efficiencyWorkers = 0u;
    u32 unclassifiedWorkers = 0u;
    u32 placementFailures = 0u;
};


class CpuTaskScope;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CpuTaskScheduler final : NoCopy{
    friend class CpuTaskScope;


public:
    using TaskHandle = CpuTaskHandle;


private:
    using TaskFunction = InplaceFunction<384u>;


private:
    enum class TaskState : u8{
        Free,
        Preparing,
        Waiting,
        Ready,
        Running,
        Children,
        Retiring,
    };

    struct TaskNode{
        TaskFunction function;
        Vector<TaskHandle, PersistentArena> dependents;
        CpuTaskScope* scope = nullptr;
        TaskHandle parent;
        CpuTaskOptions options;
        usize dependencies = 0u;
        usize children = 0u;
        u32 generation = 1u;
        u32 next = TaskHandle::s_InvalidIndex;
        TaskState state = TaskState::Free;
        bool canceled = false;

        explicit TaskNode(PersistentArena& arena);
    };

    struct ReadyQueue{
        u32 head = TaskHandle::s_InvalidIndex;
        u32 tail = TaskHandle::s_InvalidIndex;
    };

    struct Execution{
        CpuTaskScheduler& scheduler;
        TaskHandle task;
        usize workerIndex;
        CpuAffinity::Enum affinity;
        Execution* previous;

        Execution(CpuTaskScheduler& owner, TaskHandle handle, usize index, CpuAffinity::Enum workerAffinity)noexcept;
        ~Execution();
    };


private:
    static constexpr usize s_QueueCount = 12u;
    static constexpr usize s_ChunksPerWorker = 4u;
    inline static thread_local Execution* s_execution = nullptr;


private:
    static u64 allocateDomainIdentity()noexcept;
    static CpuTaskSchedulerConfig workerConfig(u32 workerCount);
    static usize queueIndex(const CpuTaskOptions& options)noexcept;


public:
    explicit CpuTaskScheduler(u32 workerCount = CpuTaskSchedulerConfig::s_AutomaticWorkerCount);
    explicit CpuTaskScheduler(const CpuTaskSchedulerConfig& config);
    ~CpuTaskScheduler()noexcept;


public:
    template<typename Func>
    TaskHandle submit(Func&& function, CpuTaskOptions options = {}){
        return submitTask(TaskFunction(Forward<Func>(function)), nullptr, options, nullptr, 0u);
    }

    template<typename Func>
    TaskHandle submit(Func&& function, TaskHandle dependency){
        return submitTask(TaskFunction(Forward<Func>(function)), nullptr, {}, &dependency, 1u);
    }

    template<typename Func>
    TaskHandle submit(Func&& function, CpuTaskOptions options, const TaskHandle* dependencies, usize count){
        return submitTask(TaskFunction(Forward<Func>(function)), nullptr, options, dependencies, count);
    }


public:
    void wait(TaskHandle handle);
    void wait();
    void drain()noexcept;
    void pumpMainThread();
    [[nodiscard]] bool isComplete(TaskHandle handle)const;
    [[nodiscard]] CpuTaskSchedulerStatistics statistics()const;
    [[nodiscard]] u64 domainIdentity()const noexcept{ return m_domainIdentity; }
    [[nodiscard]] u32 workerThreadCount()const noexcept{ return m_workerCount; }
    [[nodiscard]] bool isParallelEnabled()const noexcept{ return m_workerCount != 0u; }
    [[nodiscard]] usize currentWorkerIndex()const noexcept{
        return s_execution && &s_execution->scheduler == this ? s_execution->workerIndex : 0u;
    }

    [[nodiscard]] CpuAffinity::Enum currentWorkerAffinity()const noexcept{
        return s_execution && &s_execution->scheduler == this ? s_execution->affinity : CpuAffinity::Any;
    }


public:
    template<typename Func>
    void parallelFor(usize begin, usize end, const Func& function){
        parallelFor(begin, end, 1u, function);
    }

    template<typename Func>
    void parallelFor(usize begin, usize end, usize grainSize, const Func& function, CpuTaskOptions options = {});


private:
    TaskHandle submitTask(
        TaskFunction&& function,
        CpuTaskScope* scope,
        CpuTaskOptions options,
        const TaskHandle* dependencies,
        usize dependencyCount
    );
    void releaseReservation(TaskHandle handle)noexcept;
    [[nodiscard]] TaskNode* resolveLocked(TaskHandle handle)const noexcept;
    void enqueueLocked(u32 index)noexcept;
    [[nodiscard]] TaskHandle claimLocked(
        CpuAffinity::Enum affinity,
        bool mainThread,
        bool cooperative,
        CpuTaskScope* preferredScope = nullptr
    )noexcept;
    [[nodiscard]] bool hasReadyLocked(
        CpuAffinity::Enum affinity,
        bool mainThread,
        bool cooperative,
        CpuTaskScope* preferredScope = nullptr
    )noexcept;
    [[nodiscard]] bool contributesToScopeLocked(u32 index, const CpuTaskScope& scope)noexcept;
    [[nodiscard]] bool queueEligible(usize queue, CpuAffinity::Enum affinity, bool mainThread, bool cooperative)const noexcept;
    void execute(TaskHandle handle, usize workerIndex, CpuAffinity::Enum affinity, bool cooperative);
    void finishBody(TaskHandle handle, bool succeeded)noexcept;
    void retire(TaskHandle handle)noexcept;
    void workerLoop(const StopToken& stop, usize workerIndex);
    [[nodiscard]] bool executeOne(bool cooperative, CpuTaskScope* preferredScope = nullptr);
    void waitScope(CpuTaskScope& scope);
    [[nodiscard]] bool isMainThread()const noexcept;
    [[nodiscard]] bool isExecuting()const noexcept{ return s_execution && &s_execution->scheduler == this; }
    void notifyProgress()noexcept;


private:
    const u64 m_domainIdentity;
    const ThreadId m_mainThread;
    PersistentArena m_arena;
    Deque<TaskNode, PersistentArena> m_nodes;
    Vector<CpuWorkerPlacement, PersistentArena> m_placements;
    Vector<u32, PersistentArena> m_workerDepth;
    Vector<u32, PersistentArena> m_searchStack;
    Vector<u64, PersistentArena> m_searchVisits;
    Vector<TaskHandle, PersistentArena> m_canceledHandles;
    ReadyQueue m_ready[s_QueueCount];
    u32 m_freeNode = TaskHandle::s_InvalidIndex;
    u32 m_workerCount = 0u;
    u32 m_busyPerformance = 0u;
    u32 m_busyEfficiency = 0u;
    u64 m_searchGeneration = 0u;
    u64 m_dispatchCount = 0u;
    mutable Futex m_mutex;
    ConditionVariableAny m_changed;
    usize m_outstanding = 0u;
    bool m_aborting = false;
    CpuTaskSchedulerStatistics m_statistics;
    Vector<JoiningThread, PersistentArena> m_workers;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CpuTaskScope final : NoCopy{
    friend class CpuTaskScheduler;


public:
    using TaskHandle = CpuTaskScheduler::TaskHandle;


public:
    explicit CpuTaskScope(CpuTaskScheduler& scheduler)noexcept
        : m_scheduler(scheduler)
    {}
    ~CpuTaskScope()noexcept;


public:
    template<typename Func>
    TaskHandle submit(Func&& function, CpuTaskOptions options = {}){
        return m_scheduler.submitTask(CpuTaskScheduler::TaskFunction(Forward<Func>(function)), this, options, nullptr, 0u);
    }

    template<typename Func>
    TaskHandle submit(Func&& function, TaskHandle dependency){
        return m_scheduler.submitTask(CpuTaskScheduler::TaskFunction(Forward<Func>(function)), this, {}, &dependency, 1u);
    }

    template<typename Func>
    TaskHandle submit(Func&& function, CpuTaskOptions options, const TaskHandle* dependencies, usize count){
        return m_scheduler.submitTask(CpuTaskScheduler::TaskFunction(Forward<Func>(function)), this, options, dependencies, count);
    }


public:
    void wait();
    void drain()noexcept;
    void cancel()noexcept;
    [[nodiscard]] CpuTaskScheduler& scheduler()const noexcept{ return m_scheduler; }


public:
    template<typename Func>
    void parallelFor(usize begin, usize end, const Func& function){
        parallelFor(begin, end, 1u, function);
    }

    template<typename Func>
    void parallelFor(usize begin, usize end, usize grainSize, const Func& function, CpuTaskOptions options = {}){
        if(begin >= end)
            return;
        const TaskHandle parent = submit([this, begin, end, grainSize, &function, options](){
            m_scheduler.parallelFor(begin, end, grainSize, function, options);
        }, options);
        if(parent.valid())
            m_scheduler.wait(parent);
    }


private:
    CpuTaskScheduler& m_scheduler;
    Atomic<usize> m_pending{ 0u };
    bool m_canceled = false;
    bool m_allowCallerWork = false;
};


template<typename Func>
void CpuTaskScheduler::parallelFor(usize begin, usize end, usize grainSize, const Func& function, CpuTaskOptions options){
    if(begin >= end)
        return;

    CpuTaskScope chunks(*this);
    chunks.m_allowCallerWork = true;
    const usize count = end - begin;
    const usize grain = Max(grainSize, static_cast<usize>(1u));
    const usize chunkCount = Min(DivideUp(count, grain), Max(static_cast<usize>(m_workerCount), static_cast<usize>(1u)) * s_ChunksPerWorker);
    const usize chunkSize = count / chunkCount;
    const usize remainder = count % chunkCount;
    bool submitted = false;
    ScopeExit cancelIncomplete([&]()noexcept{
        if(!submitted)
            chunks.cancel();
    });
    for(usize chunk = 0u; chunk < chunkCount; ++chunk){
        const usize first = begin + chunk * chunkSize + Min(chunk, remainder);
        const usize last = first + chunkSize + (chunk < remainder ? 1u : 0u);
        if(!chunks.submit(
            [&function, first, last](){
                for(usize index = first; index < last; ++index)
                    function(index);
            },
            options
        ).valid())
            throw RuntimeException("CPU task scheduler rejected a parallel range");
    }
    chunks.wait();
    submitted = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

