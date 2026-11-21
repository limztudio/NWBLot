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
    template<usize InlineStorageSize>
    class TaskCallable final : NoCopy{
    private:
        struct Storage{
            alignas(MaxAlign) u8 data[InlineStorageSize];
        };

        using InvokeFn = void(*)(Storage&);
        using DestroyFn = void(*)(Storage&)noexcept;
        using MoveFn = void(*)(Storage&, Storage&)noexcept;

        struct Ops{
            InvokeFn invoke = nullptr;
            DestroyFn destroy = nullptr;
            MoveFn move = nullptr;
        };


    public:
        inline TaskCallable()noexcept = default;
        inline ~TaskCallable()noexcept{ reset(); }
        inline TaskCallable(TaskCallable&& rhs)noexcept{
            moveFrom(Move(rhs));
        }
        template<
            typename Func,
            typename FuncType = Decay_T<Func>,
            typename = EnableIf_T<
                !IsSame_V<FuncType, TaskCallable>
                && IsConstructible_V<FuncType, Func&&>
                && IsInvocableR_V<void, FuncType&>
                && IsNothrowMoveConstructible_V<FuncType>
                && IsNothrowDestructible_V<FuncType>
            >
        >
        inline explicit TaskCallable(Func&& func)noexcept(IsNothrowConstructible_V<FuncType, Func&&>){
            assign(Forward<Func>(func));
        }


    public:
        inline TaskCallable& operator=(TaskCallable&& rhs)noexcept{
            if(this != &rhs){
                reset();
                moveFrom(Move(rhs));
            }
            return *this;
        }

        template<
            typename Func,
            typename FuncType = Decay_T<Func>,
            typename = EnableIf_T<
                !IsSame_V<FuncType, TaskCallable>
                && IsConstructible_V<FuncType, Func&&>
                && IsInvocableR_V<void, FuncType&>
                && IsNothrowMoveConstructible_V<FuncType>
                && IsNothrowDestructible_V<FuncType>
            >
        >
        inline TaskCallable& operator=(Func&& func)noexcept(IsNothrowConstructible_V<FuncType, Func&&>){
            reset();
            assign(Forward<Func>(func));
            return *this;
        }

        inline explicit operator b8()const noexcept{ return m_ops != nullptr; }

        inline void operator()()const{
            FOUNDATION_ASSERT_MSG(m_ops != nullptr, FOUNDATION_TEXT("TaskCallable invoked without target"));
            m_ops->invoke(m_storage);
        }


    public:
        inline void reset()noexcept{
            if(!m_ops)
                return;
            if(m_ops->destroy)
                m_ops->destroy(m_storage);
            m_ops = nullptr;
            m_trivialBytes = 0u;
        }

        template<typename Func>
        inline void emplace(Func&& func)noexcept(IsNothrowConstructible_V<Decay_T<Func>, Func&&>){
            reset();
            assign(Forward<Func>(func));
        }


    private:
        template<typename Func>
        inline void assign(Func&& func)noexcept(IsNothrowConstructible_V<Decay_T<Func>, Func&&>){
            using FuncType = Decay_T<Func>;

            static_assert(sizeof(FuncType) <= InlineStorageSize, "TaskCallable capture size exceeds inline storage");
            static_assert(alignof(FuncType) <= alignof(Storage), "TaskCallable capture alignment exceeds inline storage");
            static_assert(IsNothrowMoveConstructible_V<FuncType>, "TaskCallable targets must be nothrow move constructible");
            static_assert(IsNothrowDestructible_V<FuncType>, "TaskCallable targets must be nothrow destructible");

            ::new(static_cast<void*>(m_storage.data)) FuncType(Forward<Func>(func));

            if constexpr(std::is_trivially_move_constructible_v<FuncType> && std::is_trivially_destructible_v<FuncType>){
                static constexpr Ops s_TrivialOps{
                    +[](Storage& storage){
                        FuncType& f = *reinterpret_cast<FuncType*>(storage.data);
                        f();
                    },
                    nullptr,
                    nullptr,
                };
                m_ops = &s_TrivialOps;
                m_trivialBytes = static_cast<u16>(sizeof(FuncType));
            }
            else{
                static constexpr Ops s_NonTrivialOps{
                    +[](Storage& storage){
                        FuncType& f = *reinterpret_cast<FuncType*>(storage.data);
                        f();
                    },
                    +[](Storage& storage)noexcept{
                        FuncType& f = *reinterpret_cast<FuncType*>(storage.data);
                        f.~FuncType();
                    },
                    +[](Storage& dst, Storage& src)noexcept{
                        FuncType& source = *reinterpret_cast<FuncType*>(src.data);
                        ::new(static_cast<void*>(dst.data)) FuncType(Move(source));
                        source.~FuncType();
                    },
                };
                m_ops = &s_NonTrivialOps;
                m_trivialBytes = 0u;
            }
        }

        inline void moveFrom(TaskCallable&& rhs)noexcept{
            if(!rhs.m_ops)
                return;
            if(rhs.m_trivialBytes != 0u)
                FOUNDATION_MEMCPY(m_storage.data, InlineStorageSize, rhs.m_storage.data, rhs.m_trivialBytes);
            else
                rhs.m_ops->move(m_storage, rhs.m_storage);
            m_ops = rhs.m_ops;
            m_trivialBytes = rhs.m_trivialBytes;
            rhs.m_ops = nullptr;
            rhs.m_trivialBytes = 0u;
        }


    private:
        const Ops* m_ops = nullptr;
        u16 m_trivialBytes = 0u;
        mutable Storage m_storage;
    };

    using TaskFunction = TaskCallable<s_TaskFunctionBytes>;
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

    static constexpr usize s_SlabShift = 6u;
    static constexpr usize s_SlabSize = 1u << s_SlabShift;
    static constexpr usize s_SlabMask = s_SlabSize - 1u;

    struct alignas(64) TaskMeta{
        CpuTaskScope* scope = nullptr;
        TaskHandle parent;
        usize dependencies = 0u;
        usize children = 0u;
        u32 generation = 1u;
        u32 next = TaskHandle::s_InvalidIndex;
        u32 latestCanceledGeneration = 0u;
        CpuTaskCost::Enum cost = CpuTaskCost::Heavy;
        CpuTaskPriority::Enum priority = CpuTaskPriority::Normal;
        CpuTaskTarget::Enum target = CpuTaskTarget::Worker;
        TaskState::Enum state = TaskState::Free;
        b8 canceled = false;
        b8 hasHandleWaiter = false;
        b8 hasDependents = false;
    };
    static_assert(sizeof(TaskMeta) == 64u, "TaskMeta must occupy exactly one 64-byte cacheline");

    struct alignas(64) TaskPayload{
        TaskFunction function;
        Vector<TaskHandle, Alloc::GlobalArena> dependents;
        Vector<u32, Alloc::GlobalArena> olderCanceledGenerations;
        CpuTaskProfileLabel profileLabel;


    public:
        TaskPayload(Alloc::GlobalArena& arena)noexcept(noexcept(TaskFunction{}) && noexcept(Vector<TaskHandle, Alloc::GlobalArena>(arena)) && noexcept(Vector<u32, Alloc::GlobalArena>(arena)))
            : dependents(arena)
            , olderCanceledGenerations(arena)
        {}
    };

    struct TaskSlab final : NoCopy{
        alignas(64) Array<TaskMeta, s_SlabSize> metas{};
        Array<TaskPayload, s_SlabSize> payloads;


    private:
        template<usize... Indices>
        TaskSlab(Alloc::GlobalArena& arena, IndexSequence<Indices...>)noexcept(noexcept(TaskPayload(arena)))
            : payloads{ (static_cast<void>(Indices), arena)... }
        {}


    public:
        explicit TaskSlab(Alloc::GlobalArena& arena)noexcept(noexcept(TaskPayload(arena)))
            : TaskSlab(arena, std::make_index_sequence<s_SlabSize>{})
        {}
    };

    struct TaskSlabPool final : NoCopy{
        Alloc::GlobalArena& arena;
        Vector<NotNull<TaskSlab*>, Alloc::GlobalArena> slabs;
        usize nodeCount = 0u;


    public:
        explicit TaskSlabPool(Alloc::GlobalArena& ownerArena)noexcept
            : arena(ownerArena)
            , slabs(ownerArena)
        {}
        ~TaskSlabPool()noexcept{
            for(usize i = slabs.size(); i > 0u; --i)
                DestroyArenaObject(arena, slabs[i - 1u].get());
        }


    public:
        [[nodiscard]] TaskMeta& meta(const u32 index)noexcept{ return slabs[index >> s_SlabShift]->metas[index & s_SlabMask]; }
        [[nodiscard]] const TaskMeta& meta(const u32 index)const noexcept{ return slabs[index >> s_SlabShift]->metas[index & s_SlabMask]; }
        [[nodiscard]] TaskPayload& payload(const u32 index)noexcept{ return slabs[index >> s_SlabShift]->payloads[index & s_SlabMask]; }
        [[nodiscard]] const TaskPayload& payload(const u32 index)const noexcept{ return slabs[index >> s_SlabShift]->payloads[index & s_SlabMask]; }
    };

    struct ReadyQueue{
        u32 head = TaskHandle::s_InvalidIndex;
        u32 tail = TaskHandle::s_InvalidIndex;
    };

    struct TaskReservation{
        TaskHandle handle;
        TaskMeta& meta;
        TaskPayload& payload;
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
    static constexpr u32 s_WorkerActive = 0u;
    static constexpr u32 s_WorkerSpinning = 1u;
    static constexpr u32 s_WorkerParked = 2u;
    static constexpr u32 s_WorkerNotified = 3u;
    static constexpr u32 s_IdleTail = Limit<u32>::s_Max - 2u;
    static constexpr u32 s_WorkerWakingTask = Limit<u32>::s_Max - 1u;
    static constexpr u32 s_WorkerNotIdle = Limit<u32>::s_Max;
    static constexpr u32 s_WorkerSpinIterations = 256u;
    static constexpr u32 s_JoinSpinIterations = 128u;
    static constexpr usize s_InlineWakePlanSlots = 32u;
    static constexpr u32 s_RangeWaiterParkedBit = 1u << 31u;
    static constexpr u32 s_RangeHelperCountMask = s_RangeWaiterParkedBit - 1u;

    struct ParallelRangeGroup;

    struct alignas(64) WorkerWaitSlot{
        Atomic<u32> state{ s_WorkerActive };
    };
    static_assert(sizeof(WorkerWaitSlot) == 64u, "WorkerWaitSlot must occupy one 64-byte cacheline");

    struct WorkerSlotArray final : NoCopy{
        Alloc::GlobalArena& arena;
        WorkerWaitSlot* slots = nullptr;
        usize count = 0u;


    public:
        explicit WorkerSlotArray(Alloc::GlobalArena& ownerArena)noexcept
            : arena(ownerArena)
        {}
        ~WorkerSlotArray()noexcept{
            reset();
        }


    public:
        void allocate(const usize slotCount){
            reset();
            if(slotCount == 0u)
                return;
            void* const memory = arena.allocate(alignof(WorkerWaitSlot), sizeof(WorkerWaitSlot) * slotCount);
            if(!memory)
                throw std::bad_alloc();
            WorkerWaitSlot* const array = static_cast<WorkerWaitSlot*>(memory);
            for(usize i = 0u; i < slotCount; ++i)
                ::new(&array[i]) WorkerWaitSlot{};
            slots = array;
            count = slotCount;
        }

        void reset()noexcept{
            if(!slots)
                return;
            for(usize i = 0u; i < count; ++i)
                slots[i].~WorkerWaitSlot();
            arena.deallocate(slots, alignof(WorkerWaitSlot), sizeof(WorkerWaitSlot) * count);
            slots = nullptr;
            count = 0u;
        }

        [[nodiscard]] WorkerWaitSlot& operator[](const usize index)noexcept{ return slots[index]; }
        [[nodiscard]] const WorkerWaitSlot& operator[](const usize index)const noexcept{ return slots[index]; }
    };

    struct ScopeWait;

    struct ClaimPipeline{
        ScopeWait* preferredScope = nullptr;
        const TaskHandle* stopOnCompleteHandle = nullptr;
        const CpuTaskScope* stopOnCompleteScope = nullptr;
        b8 stopWhenNoOutstanding = false;
        b8 mainThread = false;
        b8 executing = false;
        b8 cooperative = false;
        b8 allowPark = false;
    };

    struct WorkerWakePlan{
        Array<u32, s_InlineWakePlanSlots> parkedSlots;
        u32 parkedCount = 0u;
        b8 overflowWakeAll = false;
    };

    struct ParallelRangeGroup final : NoCopy{
        alignas(64) NotNull<const void*> context;
        NotNull<RangeFunction> invoke;
        usize begin = 0u;
        usize chunkCount = 0u;
        usize chunkSize = 0u;
        usize remainder = 0u;
        TaskHandle anchorHandle;
        CpuTaskScope* scope = nullptr;
        CpuTaskScope* lazyChildScope = nullptr;
        void* lazyChildScopeStorage = nullptr;
        CpuTaskOptions options{};
        ParallelRangeGroup* next = nullptr;
        u32 remainingHelperClaims = 0u;
        Atomic<b8> linked{ false };
        b8 fastUnscoped = false;

        alignas(64) Atomic<usize> nextChunk{ 0u };

        alignas(64) Atomic<u32> activeHelpers{ 0u };

        alignas(64) Atomic<b8> hasChildScope{ false };


    public:
        ParallelRangeGroup(
            const NotNull<const void*> rangeContext,
            const NotNull<RangeFunction> rangeInvoke,
            const usize rangeBegin,
            const usize rangeChunkCount,
            const usize rangeChunkSize,
            const usize rangeRemainder
        )noexcept
            : context(rangeContext)
            , invoke(rangeInvoke)
            , begin(rangeBegin)
            , chunkCount(rangeChunkCount)
            , chunkSize(rangeChunkSize)
            , remainder(rangeRemainder)
        {}
    };

    struct ReadyProfile{
        Timer ready;
        u64 frameIndex = 0u;
        u64 captureEpoch = 0u;
    };

    using ProfileSample = CpuTaskProfileBuffer::Sample;

    struct ClaimedExecution{
        ParallelRangeGroup* rangeGroup = nullptr;
        TaskHandle handle;
        TaskMeta* meta = nullptr;
        TaskPayload* payload = nullptr;
        Optional<ProfileSample> profile;
        Optional<ReadyProfile> readyProfile;
        WorkerWakePlan wakePlan;
        b8 invoke = false;
        b8 parkedFromExecute = false;


    public:
        [[nodiscard]] b8 hasWork()const noexcept{ return rangeGroup != nullptr || meta != nullptr; }

        void resetFast()noexcept{
            rangeGroup = nullptr;
            handle = {};
            meta = nullptr;
            payload = nullptr;
            if(profile)
                profile.reset();
            if(readyProfile)
                readyProfile.reset();
            wakePlan.parkedCount = 0u;
            wakePlan.overflowWakeAll = false;
            invoke = false;
            parkedFromExecute = false;
        }
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
        ParallelRangeGroup* rangeGroup = nullptr;
        b8* spawnedOut = nullptr;
        b8 spawnedChildren = false;


    public:
        Execution(CpuTaskScheduler& owner, TaskHandle handle, usize index, CpuAffinity::Enum workerAffinity, b8* spawned = nullptr, ParallelRangeGroup* activeRangeGroup = nullptr)noexcept;
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
    static u64 AllocateThreadIdentity()noexcept;
    [[nodiscard]] static FOUNDATION_INLINE u64 CurrentThreadIdentity()noexcept{
        thread_local const u64 s_Identity = AllocateThreadIdentity();
        return s_Identity;
    }
    static CpuTaskSchedulerConfig WorkerConfig(u32 workerCount)noexcept;
    static usize QueueIndex(const CpuTaskOptions& options)noexcept;
    static usize QueueIndex(CpuTaskCost::Enum cost, CpuTaskPriority::Enum priority, CpuTaskTarget::Enum target)noexcept;


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
    [[nodiscard]] TaskMeta* resolveLocked(TaskHandle handle)noexcept;
    [[nodiscard]] const TaskMeta* resolveLocked(TaskHandle handle)const noexcept;
    [[nodiscard]] b8 wasCanceledLocked(TaskHandle handle)const noexcept;
    [[nodiscard]] b8 isScopeCanceledLocked(const CpuTaskScope* scope)const noexcept;
    [[nodiscard]] b8 isRangeCanceled(const CpuTaskScope* scope)const noexcept;
    void enqueueLocked(u32 index, TaskMeta& meta)noexcept;
    [[nodiscard]] u16 eligibleQueueMaskLocked(CpuAffinity::Enum affinity, b8 mainThread, b8 executing, b8 cooperative)const noexcept;
    [[nodiscard]] u32 claimTaskLocked(u16 candidateMask, CpuAffinity::Enum affinity, ScopeWait* preferredScope, TaskMeta*& outMeta)noexcept;
    [[nodiscard]] ParallelRangeGroup* claimRangeLocked(u16 eligibleMask, ScopeWait* preferredScope)noexcept;
    [[nodiscard]] b8 claimNextLocked(CpuAffinity::Enum affinity, b8 mainThread, b8 executing, b8 cooperative, ScopeWait* preferredScope, usize workerIndex, ClaimedExecution& outClaim, b8 countDepth = true)noexcept;
    [[nodiscard]] b8 hasReadyLocked(CpuAffinity::Enum affinity, b8 mainThread, b8 executing, b8 cooperative, ScopeWait* preferredScope = nullptr)noexcept;
    [[nodiscard]] u32 findScopeReadyLocked(usize queue, ScopeWait& wait, u32& previous)noexcept;
    void invalidateScopeSearchLocked(b8 publication)noexcept;
    void ensureSearchBuffersLocked();
    [[nodiscard]] b8 contributesToScopeLocked(u32 index, const ScopeWait& wait);
    void addActiveDepthLocked(usize workerIndex, CpuAffinity::Enum affinity, b8& outReachedSaturation)noexcept;
    void removeActiveDepthLocked(usize workerIndex, CpuAffinity::Enum affinity)noexcept;
    void beginClaimedLocked(u32 index, TaskMeta& meta, usize workerIndex, CpuAffinity::Enum affinity, b8 cooperative, b8 computeWakePlan, ClaimedExecution& outClaim, b8 countDepth = true)noexcept;
    void executeClaimed(
        ClaimedExecution& claimed,
        usize workerIndex,
        CpuAffinity::Enum affinity,
        ClaimedExecution* nextClaim = nullptr,
        const ClaimPipeline* pipeline = nullptr
    );
    void retireNodeLocked(TaskHandle handle, TaskMeta& meta, TaskHandle& outParentHandle, TaskMeta*& outParentToRetire, TaskPayload*& outParentPayload, b8& outMadeReady, b8& outScopeCompleted, b8& outHandleWaited)noexcept;
    void retire(TaskHandle handle, TaskMeta& first, TaskPayload& firstPayload)noexcept;
    void unlinkRangeGroupLocked(ParallelRangeGroup& group)noexcept;
    void waitRangeHelpers(ParallelRangeGroup& group)noexcept;
    void pushIdleWorkerLocked(usize slotIndex, CpuAffinity::Enum affinity)noexcept;
    [[nodiscard]] b8 popIdleWorkerLocked(CpuAffinity::Enum affinity, WorkerWakePlan& outPlan, b8 countWaking)noexcept;
    void waitWorkerSlot(usize slotIndex)noexcept;
    void waitJoinEpoch(u32 expectedEpoch)noexcept;
    void workerLoop(const StopToken& stop, usize workerIndex);
    [[nodiscard]] b8 executeOne(b8 cooperative, ScopeWait* preferredScope = nullptr);
    void drainTask(TaskHandle handle)noexcept;
    void validateWaitLocked(TaskHandle handle, const CpuTaskScope* scope);
    void waitScope(CpuTaskScope& scope);
    [[nodiscard]] b8 isMainThread()const noexcept{ return CurrentThreadIdentity() == m_mainThreadIdentity; }
    [[nodiscard]] b8 isExecuting()const noexcept{ return s_Execution && &s_Execution->scheduler == this; }
    void takeWorkerWakePlanLocked(WorkerWakePlan& outPlan)noexcept;
    void takeRangeWakePlanLocked(ParallelRangeGroup& range, u32 maxWake, WorkerWakePlan& outPlan)noexcept;
    [[nodiscard]] b8 publishJoinWakeLocked(b8 condition)noexcept;
    void notifyWorkers(const WorkerWakePlan& wakePlan)noexcept;
    void notifyProgress(const WorkerWakePlan& wakePlan, b8 wakeJoiners)noexcept;
    void notifyProgress()noexcept;
    void profileReadyLocked(u32 index)noexcept;
    [[nodiscard]] ProfileSample prepareProfileLocked(CpuTaskProfileKind::Enum kind, TaskHandle task, CpuTaskProfileLabel label, usize workerIndex, CpuAffinity::Enum affinity)const noexcept;
    void finishProfileLocked(const ProfileSample& sample, Timer end)noexcept;
    void beginLockedSearch();


private:
    alignas(64) const u64 m_domainIdentity;
    const u64 m_mainThreadIdentity;
    Alloc::GlobalArena m_arena;
    Vector<CpuWorkerPlacement, Alloc::GlobalArena> m_placements;
    u32 m_workerCount = 0u;
    alignas(64) Atomic<b8> m_abortingAtomic{ false };
    Atomic<b8> m_stoppingAtomic{ false };
    Atomic<b8> m_profileEnabled{ false };
    alignas(64) Atomic<u32> m_joinWaitersAtomic{ 0u };
    alignas(64) Atomic<u32> m_activeFastRanges{ 0u };
    Atomic<u64> m_fastRangeCompleted{ 0u };

    alignas(64) TaskSlabPool m_slabs;

    alignas(64) mutable Futex m_mutex;

    Array<ReadyQueue, s_QueueCount> m_ready{};
    u16 m_nonEmptyReadyMask = 0u;
    Array<u32, s_CostValueCount> m_readyWorkerCosts{};
    Array<u32, s_AffinitySlotCount> m_idleHead{ { s_IdleTail, s_IdleTail, s_IdleTail } };
    Array<u32, s_AffinitySlotCount> m_idleCount{};
    Array<u32, s_AffinitySlotCount> m_wakingWorkers{};
    u32 m_totalIdleWorkers = 0u;
    u32 m_freeNode = TaskHandle::s_InvalidIndex;
    u32 m_busyPerformance = 0u;
    u32 m_busyEfficiency = 0u;
    u32 m_joinWaiters = 0u;
    usize m_outstanding = 0u;
    u64 m_dispatchCount = 0u;
    ParallelRangeGroup* m_activeRangeHead = nullptr;
    b8 m_aborting = false;
    b8 m_stopping = false;
    b8 m_scopeSearchActive = false;
    CpuTaskSchedulerStatistics m_statistics;
    Vector<u32, Alloc::GlobalArena> m_idleNext;
    Vector<u32, Alloc::GlobalArena> m_workerDepth;

    Vector<u32, Alloc::GlobalArena> m_searchStack;
    Vector<u64, Alloc::GlobalArena> m_searchVisits;
    Vector<u64, Alloc::GlobalArena> m_scopeContributionVisits;
    CpuTaskProfileBuffer m_profileBuffer;
    Vector<ReadyProfile, Alloc::GlobalArena> m_readyProfiles;
    u64 m_searchGeneration = 0u;
    u64 m_nextScopeIdentity = 0u;
    u64 m_scopeSearchIdentity = 0u;
    u64 m_scopeSearchGeneration = s_ScopeSearchGenerationStep;
    u64 m_scopePublicationGeneration = 1u;
    u64 m_profileEpoch = 0u;
    u64 m_profileFrameIndex = 0u;

    alignas(64) Atomic<u32> m_joinEpoch{ 0u };
    alignas(64) Atomic<u32> m_rangeHelperEpoch{ 0u };
    alignas(64) Atomic<usize> m_outstandingAtomic{ 0u };
    WorkerSlotArray m_slots;
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

    template<typename Func>
    Expected<void, CpuTaskAdmissionError::Enum> parallelRange(usize begin, usize end, usize grainSize, const Func& function, CpuTaskOptions options = {}){
        if(begin >= end)
            return {};

        return m_scheduler.parallelRange(
            begin,
            end,
            grainSize,
            options,
            MakeNotNull(static_cast<const void*>(&function)),
            NotNull<CpuTaskScheduler::RangeFunction>([](NotNull<const void*> context, const usize first, const usize last)noexcept(noexcept((*static_cast<const Func*>(context.get()))(first, last))){
                (*static_cast<const Func*>(context.get()))(first, last);
            }),
            this
        );
    }


private:
    CpuTaskScheduler& m_scheduler;
    CpuTaskScope* m_parentScope = nullptr;
    const CpuTaskProfileLabel m_profileLabel;
    u64 m_searchIdentity = 0u;
    Atomic<b8> m_canceled{ false };
    b8 m_allowCallerWork = false;
    alignas(64) Atomic<usize> m_pending{ 0u };
};

[[nodiscard]] CpuTaskScheduler& SharedCpuTaskScheduler();

inline b8 CpuTaskScheduler::isRangeCanceled(const CpuTaskScope* scope)const noexcept{
    if(m_abortingAtomic.load(MemoryOrder::relaxed))
        return true;
    while(scope){
        if(scope->m_canceled.load(MemoryOrder::relaxed))
            return true;
        scope = scope->m_parentScope;
    }
    return false;
}


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

