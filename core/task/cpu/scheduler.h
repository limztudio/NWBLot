// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "profiling.h"

#include <core/alloc/general.h>

#include <global/timer.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CpuTaskScope;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CpuTaskScheduler final : NoCopy{
    friend class CpuTaskScope;


public:
    using TaskHandle = CpuTaskHandle;


private:
    using TaskFunction = InplaceFunction<384u>;
    using RangeFunction = void(*)(const void*, usize, usize);


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
        Vector<TaskHandle, Alloc::GlobalArena> dependents;
        Vector<u32, Alloc::GlobalArena> olderCanceledGenerations;
        CpuTaskScope* scope = nullptr;
        TaskHandle parent;
        CpuTaskOptions options;
        usize dependencies = 0u;
        usize children = 0u;
        u32 generation = 1u;
        u32 latestCanceledGeneration = 0u;
        u32 next = TaskHandle::s_InvalidIndex;
        TaskState state = TaskState::Free;
        bool canceled = false;

        explicit TaskNode(Alloc::GlobalArena& arena);
    };

    struct ReadyQueue{
        // Intrusive MPMC links: every producer publication and consumer claim holds m_mutex.
        u32 head = TaskHandle::s_InvalidIndex;
        u32 tail = TaskHandle::s_InvalidIndex;
    };

    struct ReadyProfile{
        Timer ready;
        u64 frameIndex = 0u;
        u64 captureEpoch = 0u;
    };

    struct ProfileLabelRecord{
        CpuTaskProfileLabel label;
        Name name;
    };

    struct ProfileSample{
        Timer begin;
        CpuTaskProfileKind::Enum kind;
        CpuTaskHandle task;
        CpuTaskProfileLabel label;
        u64 frameIndex;
        u64 captureEpoch;
        usize workerIndex;
        CpuAffinity::Enum affinity;
    };

    class ProfileMeasure final : NoCopy{
    public:
        ProfileMeasure(CpuTaskScheduler& scheduler, CpuTaskProfileKind::Enum kind, TaskHandle task = {}, CpuTaskProfileLabel label = {});
        ~ProfileMeasure()noexcept;


    private:
        CpuTaskScheduler& m_scheduler;
        Optional<ProfileSample> m_sample;
    };

    struct ScopeWait{
        CpuTaskScope& scope;
        u64 identity = 0u;
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
    static u64 allocateProfileLabelIdentity()noexcept;
    static CpuTaskSchedulerConfig workerConfig(u32 workerCount);
    static usize queueIndex(const CpuTaskOptions& options)noexcept;


public:
    explicit CpuTaskScheduler(u32 workerCount = CpuTaskSchedulerConfig::s_AutomaticWorkerCount);
    explicit CpuTaskScheduler(const CpuTaskSchedulerConfig& config);
    ~CpuTaskScheduler()noexcept(false);


public:
    // Concurrent submission from any thread; producers must finish before destruction.
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
    // Terminal cleanup: stop admission and skip queued callbacks before joining active work.
    void drain()noexcept;
    void pumpMainThread();
    [[nodiscard]] bool isComplete(TaskHandle handle)const;
    [[nodiscard]] CpuTaskSchedulerStatistics statistics()const;
    // Register labels during setup; reuse handles in options and scopes.
    [[nodiscard]] CpuTaskProfileLabel registerProfileLabel(const Name& name);
    // Capture changes discard buffered events; zero capacity disables profiling.
    void setProfiling(bool enabled, u64 frameIndex = 0u);
    [[nodiscard]] usize readProfileEvents(CpuTaskProfileEvent* output, usize capacity)noexcept;
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
    [[nodiscard]] TaskHandle reserveTaskLocked();
    TaskHandle submitTask(
        TaskFunction&& function,
        CpuTaskScope* scope,
        CpuTaskOptions options,
        const TaskHandle* dependencies,
        usize dependencyCount
    );
    void parallelRange(usize begin, usize end, usize grainSize, CpuTaskOptions options, const void* context, RangeFunction invoke);
    void releaseReservation(TaskHandle handle)noexcept;
    [[nodiscard]] TaskNode* resolveLocked(TaskHandle handle)const noexcept;
    [[nodiscard]] bool wasCanceledLocked(TaskHandle handle)const noexcept;
    void enqueueLocked(u32 index)noexcept;
    [[nodiscard]] TaskHandle claimLocked(
        CpuAffinity::Enum affinity,
        bool mainThread,
        bool cooperative,
        const ScopeWait* preferredScope = nullptr
    )noexcept;
    [[nodiscard]] bool hasReadyLocked(
        CpuAffinity::Enum affinity,
        bool mainThread,
        bool cooperative,
        const ScopeWait* preferredScope = nullptr
    )noexcept;
    void invalidateScopeSearchLocked()noexcept;
    [[nodiscard]] bool contributesToScopeLocked(u32 index, const ScopeWait& wait)noexcept;
    [[nodiscard]] bool queueEligible(usize queue, CpuAffinity::Enum affinity, bool mainThread, bool cooperative)const noexcept;
    void execute(TaskHandle handle, usize workerIndex, CpuAffinity::Enum affinity, bool cooperative);
    void finishBody(TaskHandle handle, bool succeeded)noexcept;
    void retire(TaskHandle handle)noexcept;
    void workerLoop(const StopToken& stop, usize workerIndex);
    [[nodiscard]] bool executeOne(bool cooperative, const ScopeWait* preferredScope = nullptr);
    void drainTask(TaskHandle handle)noexcept;
    void validateWaitLocked(TaskHandle handle, const CpuTaskScope* scope);
    void waitScope(CpuTaskScope& scope);
    [[nodiscard]] bool isMainThread()const noexcept;
    [[nodiscard]] bool isExecuting()const noexcept{ return s_execution && &s_execution->scheduler == this; }
    [[nodiscard]] u32 workerWakeMaskLocked()const noexcept;
    void notifyWorkers(u32 wakeMask)noexcept;
    void notifyProgress(u32 wakeMask)noexcept;
    void notifyProgress()noexcept;
    void profileReadyLocked(u32 index)noexcept;
    [[nodiscard]] ProfileSample prepareProfileLocked(
        CpuTaskProfileKind::Enum kind,
        TaskHandle task,
        CpuTaskProfileLabel label,
        usize workerIndex,
        CpuAffinity::Enum affinity
    )const noexcept;
    void finishProfileLocked(const ProfileSample& sample, Timer end)noexcept;


private:
    const u64 m_domainIdentity;
    const ThreadId m_mainThread;
    const usize m_profileEventCapacity;
    Alloc::GlobalArena m_arena;
    Deque<TaskNode, Alloc::GlobalArena> m_nodes;
    Vector<CpuWorkerPlacement, Alloc::GlobalArena> m_placements;
    Vector<u32, Alloc::GlobalArena> m_workerDepth;
    Vector<u32, Alloc::GlobalArena> m_searchStack;
    Vector<u64, Alloc::GlobalArena> m_searchVisits;
    Vector<u64, Alloc::GlobalArena> m_scopeNegativeVisits;
    Vector<ProfileLabelRecord, Alloc::GlobalArena> m_profileLabels;
    Vector<CpuTaskProfileEvent, Alloc::GlobalArena> m_profileEvents;
    Vector<ReadyProfile, Alloc::GlobalArena> m_readyProfiles;
    ReadyQueue m_ready[s_QueueCount];
    u32 m_readyWorkerCosts[3u]{};
    u32 m_sleepingWorkers[3u]{};
    u32 m_freeNode = TaskHandle::s_InvalidIndex;
    u32 m_workerCount = 0u;
    u32 m_busyPerformance = 0u;
    u32 m_busyEfficiency = 0u;
    u64 m_searchGeneration = 0u;
    u64 m_nextScopeWaitIdentity = 0u;
    u64 m_scopeSearchWaitIdentity = 0u;
    u64 m_scopeSearchGeneration = 1u;
    u64 m_dispatchCount = 0u;
    mutable Futex m_mutex;
    ConditionVariableAny m_changed;
    ConditionVariableAny m_workerChanged[3u];
    usize m_outstanding = 0u;
    bool m_aborting = false;
    CpuTaskSchedulerStatistics m_statistics;
    Atomic<bool> m_profileEnabled{ false };
    u64 m_profileEpoch = 0u;
    u64 m_profileFrameIndex = 0u;
    u64 m_profileRecordedEvents = 0u;
    u64 m_profileDroppedEvents = 0u;
    usize m_profileRead = 0u;
    usize m_profileCount = 0u;
    Vector<JoiningThread, Alloc::GlobalArena> m_workers;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CpuTaskScope final : NoCopy{
    friend class CpuTaskScheduler;


public:
    using TaskHandle = CpuTaskScheduler::TaskHandle;


public:
    explicit CpuTaskScope(CpuTaskScheduler& scheduler, CpuTaskProfileLabel label = {})noexcept
        : m_scheduler(scheduler)
        , m_profileLabel(label)
    {}
    ~CpuTaskScope()noexcept(false);


public:
    // Scopes accept concurrent producers; join before destroying the scope.
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
    // Terminal cleanup aborts the shared service; use cancel() for ordinary scope cancellation.
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
        TaskHandle parent;
        ScopeExit drainOnFailure([this, &parent]()noexcept{ m_scheduler.drainTask(parent); });

        parent = submit([this, begin, end, grainSize, &function, options](){
            m_scheduler.parallelFor(begin, end, grainSize, function, options);
        }, options);
        if(parent.valid())
            m_scheduler.wait(parent);
        drainOnFailure.release();
    }


private:
    CpuTaskScheduler& m_scheduler;
    const CpuTaskProfileLabel m_profileLabel;
    Atomic<usize> m_pending{ 0u };
    bool m_canceled = false;
    bool m_allowCallerWork = false;
};


template<typename Func>
void CpuTaskScheduler::parallelFor(usize begin, usize end, usize grainSize, const Func& function, CpuTaskOptions options){
    if(begin >= end)
        return;

    const auto range = [&function](const usize first, const usize last){
        for(usize index = first; index < last; ++index)
            function(index);
    };
    parallelRange(begin, end, grainSize, options, &range, [](const void* context, const usize first, const usize last){
        (*static_cast<const decltype(range)*>(context))(first, last);
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

