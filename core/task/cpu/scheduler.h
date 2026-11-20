// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "profiling.h"
#include "profile_buffer.h"

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
    static constexpr usize s_TaskFunctionBytes = 384u;


private:
    using TaskFunction = InplaceFunction<s_TaskFunctionBytes>;
    using RangeFunction = void(*)(NotNull<const void*>, usize, usize);


private:
    struct TaskState{
        enum Enum : u8{
            Free,
            Preparing,
            Waiting,
            Ready,
            Running,
            Children,
            Retiring,
        };
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
        b8 canceled = false;
        TaskState::Enum state = TaskState::Free;


    public:
        explicit TaskNode(Alloc::GlobalArena& arena)noexcept(noexcept(TaskFunction{}) && noexcept(Vector<TaskHandle, Alloc::GlobalArena>(arena)) && noexcept(Vector<u32, Alloc::GlobalArena>(arena)));
    };

    struct ReadyQueue{
        u32 head = TaskHandle::s_InvalidIndex;
        u32 tail = TaskHandle::s_InvalidIndex;
    };

    struct TaskReservation{
        TaskHandle handle;
        TaskNode& node;
    };

private:
    static constexpr usize s_PrioritySlotCount = 3u;
    static constexpr usize s_CostSlotCount = 4u;
    static constexpr usize s_CostValueCount = 3u;
    static constexpr usize s_AffinitySlotCount = 3u;
    static constexpr usize s_MainThreadTargetSlot = 3u;
    static constexpr usize s_QueueCount = s_PrioritySlotCount * s_CostSlotCount;
    static constexpr usize s_ChunksPerWorker = 4u;
    static constexpr u64 s_ScopeSearchGenerationStep = 2u;


private:
    struct WorkerWakePlan{
        Array<u32, s_AffinitySlotCount> counts{};
        Array<b8, s_AffinitySlotCount> wakeAll{};
    };

    struct ReadyProfile{
        Timer ready;
        u64 frameIndex = 0u;
        u64 captureEpoch = 0u;
    };

    using ProfileSample = CpuTaskProfileBuffer::Sample;

    struct ClaimedExecution{
        TaskHandle handle;
        TaskNode* node = nullptr;
        Optional<ProfileSample> profile;
        Optional<ReadyProfile> readyProfile;
        WorkerWakePlan wakePlan;
        b8 invoke = false;
    };

    class ProfileMeasure final : NoCopy{
    public:
        ProfileMeasure(CpuTaskScheduler& scheduler, CpuTaskProfileKind::Enum kind, TaskHandle task = {}, CpuTaskProfileLabel label = {})noexcept;
        ~ProfileMeasure()noexcept;


    private:
        CpuTaskScheduler& m_scheduler;
        Optional<ProfileSample> m_sample;
    };

    struct Execution{
        CpuTaskScheduler& scheduler;
        Execution* previous;
        TaskHandle task;
        usize workerIndex;
        CpuAffinity::Enum affinity;
        b8* spawnedOut = nullptr;
        b8 spawnedChildren = false;


    public:
        Execution(CpuTaskScheduler& owner, TaskHandle handle, usize index, CpuAffinity::Enum workerAffinity, b8* spawned = nullptr)noexcept;
        ~Execution()noexcept;
    };


private:
    inline static thread_local Execution* s_Execution = nullptr;


private:
    struct ScopeWait{
        CpuTaskScope& scope;
        u64 scopeIdentity = 0u;
        u64 publicationGeneration = 0u;
        Array<TaskHandle, s_QueueCount> unrelatedAnchors{};
    };


private:
    static u64 AllocateDomainIdentity()noexcept;
    static CpuTaskSchedulerConfig WorkerConfig(u32 workerCount)noexcept;
    static usize QueueIndex(const CpuTaskOptions& options)noexcept;


public:
    explicit CpuTaskScheduler(u32 workerCount = CpuTaskSchedulerConfig::s_AutomaticWorkerCount);
    explicit CpuTaskScheduler(const CpuTaskSchedulerConfig& config);
    ~CpuTaskScheduler()noexcept(false);


public:
    template<typename Func>
    [[nodiscard]] Expected<TaskHandle, CpuTaskAdmissionError::Enum> submit(Func&& function, CpuTaskOptions options = {}){
        return submitTask(TaskFunction(Forward<Func>(function)), nullptr, options, nullptr, 0u);
    }

    template<typename Func>
    [[nodiscard]] Expected<TaskHandle, CpuTaskAdmissionError::Enum> submit(Func&& function, TaskHandle dependency){
        return submitTask(TaskFunction(Forward<Func>(function)), nullptr, {}, &dependency, 1u);
    }

    template<typename Func>
    [[nodiscard]] Expected<TaskHandle, CpuTaskAdmissionError::Enum> submit(Func&& function, CpuTaskOptions options, const TaskHandle* dependencies, usize count){
        return submitTask(TaskFunction(Forward<Func>(function)), nullptr, options, dependencies, count);
    }


public:
    void wait(TaskHandle handle);
    void wait();
    void drain()noexcept;
    void pumpMainThread();
    [[nodiscard]] b8 isComplete(TaskHandle handle)const;
    [[nodiscard]] CpuTaskSchedulerStatistics statistics()const noexcept;
    [[nodiscard]] CpuTaskProfileLabel registerProfileLabel(const Name& name);
    void setProfiling(b8 enabled, u64 frameIndex = 0u);
    [[nodiscard]] usize readProfileEvents(CpuTaskProfileEvent* output, usize capacity)noexcept;
    [[nodiscard]] u64 domainIdentity()const noexcept{ return m_domainIdentity; }

    [[nodiscard]] u32 workerThreadCount()const noexcept{ return m_workerCount; }

    [[nodiscard]] b8 isParallelEnabled()const noexcept{ return m_workerCount != 0u; }

    [[nodiscard]] usize currentWorkerIndex()const noexcept{ return s_Execution && &s_Execution->scheduler == this ? s_Execution->workerIndex : 0u; }

    [[nodiscard]] CpuAffinity::Enum currentWorkerAffinity()const noexcept{ return s_Execution && &s_Execution->scheduler == this ? s_Execution->affinity : CpuAffinity::Any; }


public:
    template<typename Func>
    [[nodiscard]] Expected<void, CpuTaskAdmissionError::Enum> parallelFor(usize begin, usize end, const Func& function);

    template<typename Func>
    [[nodiscard]] Expected<void, CpuTaskAdmissionError::Enum> parallelFor(usize begin, usize end, usize grainSize, const Func& function, CpuTaskOptions options = {});


private:
    [[nodiscard]] Expected<TaskReservation, CpuTaskAdmissionError::Enum> reserveTaskLocked();
    [[nodiscard]] Expected<TaskHandle, CpuTaskAdmissionError::Enum> submitTask(TaskFunction&& function, CpuTaskScope* scope, CpuTaskOptions options, const TaskHandle* dependencies, usize dependencyCount);
    [[nodiscard]] Expected<void, CpuTaskAdmissionError::Enum> parallelRange(usize begin, usize end, usize grainSize, CpuTaskOptions options, NotNull<const void*> context, NotNull<RangeFunction> invoke, CpuTaskScope* callerScope = nullptr);
    [[nodiscard]] TaskNode* resolveLocked(TaskHandle handle)noexcept;
    [[nodiscard]] const TaskNode* resolveLocked(TaskHandle handle)const noexcept;
    [[nodiscard]] b8 wasCanceledLocked(TaskHandle handle)const noexcept;
    [[nodiscard]] b8 isScopeCanceledLocked(const CpuTaskScope* scope)const noexcept;
    [[nodiscard]] b8 isRangeCanceled(const CpuTaskScope* scope)const noexcept;
    void enqueueLocked(u32 index)noexcept;
    [[nodiscard]] u16 eligibleQueueMaskLocked(CpuAffinity::Enum affinity, b8 mainThread, b8 cooperative)const noexcept;
    [[nodiscard]] TaskHandle claimLocked(CpuAffinity::Enum affinity, b8 mainThread, b8 cooperative, ScopeWait* preferredScope = nullptr)noexcept;
    [[nodiscard]] b8 hasReadyLocked(CpuAffinity::Enum affinity, b8 mainThread, b8 cooperative, ScopeWait* preferredScope = nullptr)noexcept;
    [[nodiscard]] u32 findScopeReadyLocked(usize queue, ScopeWait& wait, u32& previous)noexcept;
    void invalidateScopeSearchLocked(b8 publication)noexcept;
    [[nodiscard]] b8 contributesToScopeLocked(u32 index, const ScopeWait& wait)noexcept;
    [[nodiscard]] b8 queueEligible(usize queue, CpuAffinity::Enum affinity, b8 mainThread, b8 cooperative)const noexcept;
    [[nodiscard]] ClaimedExecution beginClaimedLocked(TaskHandle handle, usize workerIndex, CpuAffinity::Enum affinity, b8 cooperative, b8 computeWakePlan = true)noexcept;
    void executeClaimed(ClaimedExecution& claimed, usize workerIndex, CpuAffinity::Enum affinity, ClaimedExecution* nextClaim = nullptr, const StopToken* stop = nullptr);
    void retireNodeLocked(TaskHandle handle, TaskNode& node, TaskHandle& outParentHandle, TaskNode*& outParentToRetire, b8& outMadeReady, b8& outScopeCompleted)noexcept;
    void retire(TaskHandle handle, TaskNode& first)noexcept;
    void workerLoop(const StopToken& stop, usize workerIndex);
    [[nodiscard]] b8 executeOne(b8 cooperative, ScopeWait* preferredScope = nullptr);
    void drainTask(TaskHandle handle)noexcept;
    void validateWaitLocked(TaskHandle handle, const CpuTaskScope* scope);
    void waitScope(CpuTaskScope& scope);
    [[nodiscard]] b8 isMainThread()const noexcept;
    [[nodiscard]] b8 isExecuting()const noexcept{ return s_Execution && &s_Execution->scheduler == this; }
    [[nodiscard]] WorkerWakePlan takeWorkerWakePlanLocked()noexcept;
    void notifyWorkers(const WorkerWakePlan& wakePlan)noexcept;
    void notifyProgress(const WorkerWakePlan& wakePlan, b8 wakeJoiners)noexcept;
    void notifyProgress()noexcept;
    void profileReadyLocked(u32 index)noexcept;
    [[nodiscard]] ProfileSample prepareProfileLocked(CpuTaskProfileKind::Enum kind, TaskHandle task, CpuTaskProfileLabel label, usize workerIndex, CpuAffinity::Enum affinity)const noexcept;
    void finishProfileLocked(const ProfileSample& sample, Timer end)noexcept;
    void beginLockedSearch()noexcept;


private:
    const u64 m_domainIdentity;
    Alloc::GlobalArena m_arena;
    Deque<TaskNode, Alloc::GlobalArena> m_nodes;
    Vector<CpuWorkerPlacement, Alloc::GlobalArena> m_placements;
    Vector<u32, Alloc::GlobalArena> m_workerDepth;
    Vector<u32, Alloc::GlobalArena> m_searchStack;
    Vector<u64, Alloc::GlobalArena> m_searchVisits;
    Vector<u64, Alloc::GlobalArena> m_scopeContributionVisits;
    CpuTaskProfileBuffer m_profileBuffer;
    Vector<ReadyProfile, Alloc::GlobalArena> m_readyProfiles;
    Array<ReadyQueue, s_QueueCount> m_ready;
    u16 m_nonEmptyReadyMask = 0u;
    Array<u32, s_CostValueCount> m_readyWorkerCosts{};
    Array<u32, s_AffinitySlotCount> m_sleepingWorkers{};
    Array<u32, s_AffinitySlotCount> m_wakingWorkers{};
    u32 m_freeNode = TaskHandle::s_InvalidIndex;
    u32 m_workerCount = 0u;
    u32 m_busyPerformance = 0u;
    u32 m_busyEfficiency = 0u;
    u64 m_searchGeneration = 0u;
    u64 m_nextScopeIdentity = 0u;
    u64 m_scopeSearchIdentity = 0u;
    u64 m_scopeSearchGeneration = s_ScopeSearchGenerationStep;
    u64 m_scopePublicationGeneration = 1u;
    u64 m_dispatchCount = 0u;
    const ThreadId m_mainThread;
    mutable Futex m_mutex;
    ConditionVariableAny m_changed;
    Array<ConditionVariableAny, s_AffinitySlotCount> m_workerChanged;
    usize m_outstanding = 0u;
    u32 m_joinWaiters = 0u;
    u32 m_handleJoinWaiters = 0u;
    CpuTaskSchedulerStatistics m_statistics;
    u64 m_profileEpoch = 0u;
    u64 m_profileFrameIndex = 0u;
    b8 m_aborting = false;
    b8 m_scopeSearchActive = false;
    Vector<JoiningThread, Alloc::GlobalArena> m_workers;
    alignas(64) Atomic<b8> m_abortingAtomic{ false };
    Atomic<b8> m_profileEnabled{ false };
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
    template<typename Func>
    Expected<TaskHandle, CpuTaskAdmissionError::Enum> submit(Func&& function, CpuTaskOptions options = {}){
        return m_scheduler.submitTask(CpuTaskScheduler::TaskFunction(Forward<Func>(function)), this, options, nullptr, 0u);
    }

    template<typename Func>
    Expected<TaskHandle, CpuTaskAdmissionError::Enum> submit(Func&& function, TaskHandle dependency){
        return m_scheduler.submitTask(CpuTaskScheduler::TaskFunction(Forward<Func>(function)), this, {}, &dependency, 1u);
    }

    template<typename Func>
    Expected<TaskHandle, CpuTaskAdmissionError::Enum> submit(Func&& function, CpuTaskOptions options, const TaskHandle* dependencies, usize count){
        return m_scheduler.submitTask(CpuTaskScheduler::TaskFunction(Forward<Func>(function)), this, options, dependencies, count);
    }


public:
    void wait(){ m_scheduler.waitScope(*this); }
    void waitCooperative(){
        const b8 previous = m_allowCallerWork;
        m_allowCallerWork = true;
        ScopeExit restore([this, previous]()noexcept{ m_allowCallerWork = previous; });

        m_scheduler.waitScope(*this);
    }
    void drain()noexcept;
    void cancel()noexcept;
    [[nodiscard]] CpuTaskScheduler& scheduler()const noexcept{ return m_scheduler; }


public:
    template<typename Func>
    Expected<void, CpuTaskAdmissionError::Enum> parallelFor(usize begin, usize end, const Func& function){
        return parallelFor(begin, end, 1u, function);
    }

    template<typename Func>
    Expected<void, CpuTaskAdmissionError::Enum> parallelFor(usize begin, usize end, usize grainSize, const Func& function, CpuTaskOptions options = {}){
        if(begin >= end)
            return {};

        const auto range = [&function](const usize first, const usize last)noexcept(noexcept(DeclVal<const Func&>()(DeclVal<usize&>()))){
            for(usize index = first; index < last; ++index)
                function(index);
        };
        return m_scheduler.parallelRange(
            begin,
            end,
            grainSize,
            options,
            MakeNotNull(static_cast<const void*>(&range)),
            NotNull<CpuTaskScheduler::RangeFunction>([](NotNull<const void*> context, const usize first, const usize last)noexcept(noexcept((*static_cast<const decltype(range)*>(context.get()))(first, last))){
                (*static_cast<const decltype(range)*>(context.get()))(first, last);
            }),
            this
        );
    }


private:
    CpuTaskScheduler& m_scheduler;
    CpuTaskScope* m_parentScope = nullptr;
    const CpuTaskProfileLabel m_profileLabel;
    Atomic<usize> m_pending{ 0u };
    u64 m_searchIdentity = 0u;
    Atomic<b8> m_canceled{ false };
    b8 m_allowCallerWork = false;
};

[[nodiscard]] CpuTaskScheduler& SharedCpuTaskScheduler();


template<typename Func>
Expected<void, CpuTaskAdmissionError::Enum> CpuTaskScheduler::parallelFor(usize begin, usize end, const Func& function){
    return parallelFor(begin, end, 1u, function);
}

template<typename Func>
Expected<void, CpuTaskAdmissionError::Enum> CpuTaskScheduler::parallelFor(usize begin, usize end, usize grainSize, const Func& function, CpuTaskOptions options){
    if(begin >= end)
        return {};

    const auto range = [&function](const usize first, const usize last)noexcept(noexcept(DeclVal<const Func&>()(DeclVal<usize&>()))){
        for(usize index = first; index < last; ++index)
            function(index);
    };
    return parallelRange(
        begin,
        end,
        grainSize,
        options,
        MakeNotNull(static_cast<const void*>(&range)),
        NotNull<RangeFunction>([](NotNull<const void*> context, const usize first, const usize last)noexcept(noexcept((*static_cast<const decltype(range)*>(context.get()))(first, last))){
            (*static_cast<const decltype(range)*>(context.get()))(first, last);
        })
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

