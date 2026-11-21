// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"
#include "arena_names.h"

#include <global/atomic_identity.h>
#include <global/bit.h>
#include <global/exception.h>
#include <global/range_partition.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_scheduler{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u64 s_BackgroundAdmissionPeriod = 32u;
inline constexpr u64 s_BackgroundAdmissionTick = 31u;
inline constexpr u64 s_NormalAdmissionPeriod = 8u;
inline constexpr u64 s_NormalAdmissionTick = 7u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CpuTaskScheduler::Execution::Execution(CpuTaskScheduler& owner, TaskHandle handle, usize index, CpuAffinity::Enum workerAffinity, b8* const spawned, ParallelRangeGroup* const activeRangeGroup)noexcept
    : scheduler(owner)
    , previous(s_Execution)
    , task(handle)
    , workerIndex(index)
    , affinity(workerAffinity)
    , rangeGroup(activeRangeGroup)
    , spawnedOut(spawned)
{
    s_Execution = this;
}

CpuTaskScheduler::Execution::~Execution()noexcept{
    if(spawnedOut)
        *spawnedOut = spawnedChildren;
    s_Execution = previous;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 CpuTaskScheduler::AllocateDomainIdentity()noexcept{
    static Atomic<u64> s_Next{ 1u };
    return NextNonWrappingIdentity(s_Next);
}

u64 CpuTaskScheduler::AllocateThreadIdentity()noexcept{
    static Atomic<u64> s_Next{ 1u };
    return NextNonWrappingIdentity(s_Next);
}

CpuTaskScheduler& SharedCpuTaskScheduler(){
    static CpuTaskScheduler s_Shared;
    return s_Shared;
}

CpuTaskSchedulerConfig CpuTaskScheduler::WorkerConfig(const u32 workerCount)noexcept{
    CpuTaskSchedulerConfig config;
    config.workerCount = workerCount;
    return config;
}

usize CpuTaskScheduler::QueueIndex(const CpuTaskOptions& options)noexcept{
    return QueueIndex(options.cost, options.priority, options.target);
}

usize CpuTaskScheduler::QueueIndex(const CpuTaskCost::Enum cost, const CpuTaskPriority::Enum priority, const CpuTaskTarget::Enum target)noexcept{
    const usize slot = target == CpuTaskTarget::MainThread ? s_MainThreadTargetSlot : static_cast<usize>(cost);
    return static_cast<usize>(priority) * s_CostSlotCount + slot;
}


CpuTaskScheduler::CpuTaskScheduler(const u32 workerCount)
    : CpuTaskScheduler(WorkerConfig(workerCount))
{}

CpuTaskScheduler::CpuTaskScheduler(const CpuTaskSchedulerConfig& config)
    : m_domainIdentity(AllocateDomainIdentity())
    , m_mainThreadIdentity(CurrentThreadIdentity())
    , m_arena(TaskArenaScope::s_CpuTaskScheduler)
    , m_placements(m_arena)
    , m_slabs(m_arena)
    , m_idleNext(m_arena)
    , m_workerDepth(m_arena)
    , m_searchStack(m_arena)
    , m_searchVisits(m_arena)
    , m_scopeContributionVisits(m_arena)
    , m_profileBuffer(m_arena, config.profileEventCapacity)
    , m_readyProfiles(m_arena)
    , m_slots(m_arena)
    , m_workers(m_arena)
{
    auto placementResult = QueryCpuWorkerPlacements();
    InteropVector<CpuWorkerPlacement> topology = placementResult ? Move(*placementResult) : InteropVector<CpuWorkerPlacement>{};
    Sort(topology.begin(), topology.end(), [](const CpuWorkerPlacement& lhs, const CpuWorkerPlacement& rhs)noexcept{
        return lhs.performanceClass > rhs.performanceClass;
    });
    const u32 available = topology.empty() ? Max(QueryCpuCoreCount(CpuAffinity::Any), 1u) : static_cast<u32>(topology.size());
    const u32 reserved = Min(config.reservedThreadCount, available);
    m_workerCount = config.workerCount == CpuTaskSchedulerConfig::s_AutomaticWorkerCount ? available - reserved : config.workerCount;
    m_placements.reserve(m_workerCount);
    const usize first = config.workerCount == CpuTaskSchedulerConfig::s_AutomaticWorkerCount ? reserved : 0u;
    for(u32 worker = 0u; worker < m_workerCount; ++worker){
        CpuWorkerPlacement placement;
        if(!topology.empty()){
            const usize usable = topology.size() - Min(first, topology.size() - 1u);
            usize selected = first + static_cast<usize>(worker) % usable;
            if(m_workerCount > 1u && static_cast<usize>(m_workerCount) < usable){
                selected = first + static_cast<usize>(worker) * (usable - 1u) / (m_workerCount - 1u);
            }
            placement = topology[selected];
            if(!config.heterogeneous)
                placement.affinity = CpuAffinity::Any;
        }
        m_placements.push_back(placement);
        switch(placement.affinity){
        case CpuAffinity::Performance: ++m_statistics.performanceWorkers; break;
        case CpuAffinity::Efficiency: ++m_statistics.efficiencyWorkers; break;
        default: ++m_statistics.unclassifiedWorkers; break;
        }
    }
    m_slots.allocate(m_workerCount);
    m_idleNext.resize(m_workerCount, s_WorkerNotIdle);
    m_workerDepth.resize(m_workerCount, 0u);
    m_workers.reserve(m_workerCount);
    for(u32 worker = 0u; worker < m_workerCount; ++worker){
        m_workers.emplace_back([this, index = static_cast<usize>(worker) + 1u](const StopToken& stop){
            workerLoop(stop, index);
        });
    }
}

CpuTaskScheduler::~CpuTaskScheduler()noexcept(false){
    if(UncaughtExceptionCount() > 0){
        drain();
        return;
    }
    ScopeExit drainOnFailure([this]()noexcept{ drain(); });

    wait();
    drainOnFailure.release();
}


void CpuTaskScheduler::pumpMainThread(){
    if(!isMainThread())
        throw RuntimeException("CPU main-thread tasks require the scheduler owner thread");
    const b8 executing = isExecuting();
    const ClaimPipeline pipeline{
        .preferredScope = nullptr,
        .stopOnCompleteHandle = nullptr,
        .stopOnCompleteScope = nullptr,
        .stopWhenNoOutstanding = false,
        .mainThread = true,
        .executing = executing,
        .cooperative = false,
        .allowPark = false,
    };
    ClaimedExecution claims[2];
    u32 active = 0u;
    {
        ScopedLock lock(m_mutex);
        (void)claimNextLocked(CpuAffinity::Any, true, executing, false, nullptr, 0u, claims[active]);
    }
    while(claims[active].hasWork()){
        const u32 next = active ^ 1u;
        claims[next].resetFast();
        executeClaimed(claims[active], 0u, CpuAffinity::Any, &claims[next], &pipeline);
        claims[active].resetFast();
        active = next;
    }
}

b8 CpuTaskScheduler::isComplete(const TaskHandle handle)const{
    if(!handle.valid())
        return true;
    if(handle.domainIdentity != m_domainIdentity)
        throw RuntimeException("CPU task query belongs to another scheduler");
    NothrowScopedLock lock(m_mutex);
    return !resolveLocked(handle);
}

CpuTaskSchedulerStatistics CpuTaskScheduler::statistics()const noexcept{
    NothrowScopedLock lock(m_mutex);
    CpuTaskSchedulerStatistics result = m_statistics;
    result.completedTasks += m_fastRangeCompleted.load(MemoryOrder::relaxed);
    result.outstandingTasks = m_outstanding + m_activeFastRanges.load(MemoryOrder::relaxed);
    result.profileEnabled = m_profileEnabled.load(MemoryOrder::relaxed);
    result.profilePendingEvents = m_profileBuffer.pending();
    result.profileRecordedEvents = m_profileBuffer.recorded();
    result.profileDroppedEvents = m_profileBuffer.dropped();
    return result;
}


Expected<CpuTaskScheduler::TaskReservation, CpuTaskAdmissionError::Enum> CpuTaskScheduler::reserveTaskLocked(){
    u32 index = m_freeNode;
    if(index == TaskHandle::s_InvalidIndex){
        if(m_slabs.nodeCount >= TaskHandle::s_InvalidIndex)
            return MakeUnexpected(CpuTaskAdmissionError::CapacityExhausted);
        index = static_cast<u32>(m_slabs.nodeCount);
        const usize slabIndex = static_cast<usize>(index) >> s_SlabShift;
        if(slabIndex >= m_slabs.slabs.size()){
            TaskSlab* const newSlab = NewArenaObject<TaskSlab>(m_arena, m_arena);
            if(!newSlab)
                return MakeUnexpected(CpuTaskAdmissionError::CapacityExhausted);
            ScopeExit destroyNewSlab([this, newSlab]()noexcept{
                DestroyArenaObjectNoexcept(m_arena, newSlab);
            });
            m_slabs.slabs.push_back(MakeNotNull(newSlab));
            destroyNewSlab.release();
        }
        if(m_profileEnabled.load(MemoryOrder::relaxed))
            m_readyProfiles.resize(m_slabs.nodeCount + 1u);
        ++m_slabs.nodeCount;
    }
    else
        m_freeNode = m_slabs.meta(index).next;
    TaskMeta& meta = m_slabs.meta(index);
    meta.state = TaskState::Preparing;
    meta.next = TaskHandle::s_InvalidIndex;
    meta.hasHandleWaiter = false;
    meta.hasDependents = false;
    return TaskReservation{ TaskHandle{ m_domainIdentity, index, meta.generation }, meta, m_slabs.payload(index) };
}

Expected<CpuTaskScheduler::TaskHandle, CpuTaskAdmissionError::Enum> CpuTaskScheduler::submitTask(TaskFunction&& function, CpuTaskScope* scope, const CpuTaskOptions options, const TaskHandle* const dependencies, const usize dependencyCount){
    if(!function)
        return MakeUnexpected(CpuTaskAdmissionError::InvalidFunction);
    if(dependencyCount != 0u && !dependencies)
        return MakeUnexpected(CpuTaskAdmissionError::InvalidDependencies);
    if(options.cost > CpuTaskCost::Light || options.priority > CpuTaskPriority::Background || options.target > CpuTaskTarget::MainThread)
        return MakeUnexpected(CpuTaskAdmissionError::InvalidOptions);

    static_assert(noexcept(DeclVal<TaskPayload&>().function = Move(function)));

    Execution* const activeExec = s_Execution;
    const b8 inScheduler = activeExec && &activeExec->scheduler == this;
    WorkerWakePlan wakePlan;
    b8 wakeJoiners = false;
    TaskHandle handle;
    {
        ScopedLock lock(m_mutex);
        if(m_aborting)
            return MakeUnexpected(CpuTaskAdmissionError::Aborting);
        if(!scope && inScheduler && !activeExec->task.valid() && activeExec->rangeGroup && activeExec->rangeGroup->fastUnscoped){
            ParallelRangeGroup& activeRange = *activeExec->rangeGroup;
            if(!activeRange.lazyChildScope){
                CpuTaskScope* const constructed = ::new(activeRange.lazyChildScopeStorage) CpuTaskScope(*this, {});
                constructed->m_allowCallerWork = true;
                activeRange.lazyChildScope = constructed;
                activeRange.hasChildScope.store(true, MemoryOrder::release);
            }
            scope = activeRange.lazyChildScope;
        }
        if(isScopeCanceledLocked(scope))
            return MakeUnexpected(CpuTaskAdmissionError::ScopeCanceled);
        for(usize depIdx = 0u; depIdx < dependencyCount; ++depIdx){
            if(dependencies[depIdx].valid() && dependencies[depIdx].domainIdentity != m_domainIdentity)
                return MakeUnexpected(CpuTaskAdmissionError::ForeignDependency);
        }
        const auto reservation = reserveTaskLocked();
        if(!reservation)
            return MakeUnexpected(reservation.error());
        handle = reservation->handle;
        TaskMeta& meta = reservation->meta;
        TaskPayload& payload = reservation->payload;
        ScopeExit release([&]()noexcept{
            meta.state = TaskState::Free;
            meta.next = m_freeNode;
            m_freeNode = handle.index;
        });

        if(meta.latestCanceledGeneration != 0u)
            ContainerDetail::ReserveGrowingCapacity(payload.olderCanceledGenerations, AddSize(payload.olderCanceledGenerations.size(), 1u));
        u64 dependencySearchGeneration = 0u;
        if(dependencyCount != 0u && inScheduler && activeExec->task.valid()){
            beginLockedSearch();
            dependencySearchGeneration = m_searchGeneration;
            const auto visit = [this](const TaskHandle candidate)noexcept{
                if(resolveLocked(candidate) && m_searchVisits[candidate.index] != m_searchGeneration){
                    m_searchVisits[candidate.index] = m_searchGeneration;
                    m_searchStack.push_back(candidate.index);
                }
            };
            visit(activeExec->task);
            for(usize cursor = 0u; cursor < m_searchStack.size(); ++cursor){
                const u32 candidateIndex = m_searchStack[cursor];
                const TaskMeta& reachable = m_slabs.meta(candidateIndex);
                if(reachable.hasDependents){
                    for(const TaskHandle dependent : m_slabs.payload(candidateIndex).dependents)
                        visit(dependent);
                }
                visit(reachable.parent);
            }
        }
        if(dependencyCount == 1u){
            if(resolveLocked(dependencies[0])){
                const u32 dependencyIndex = dependencies[0].index;
                if(dependencySearchGeneration != 0u && m_searchVisits[dependencyIndex] == dependencySearchGeneration)
                    return MakeUnexpected(CpuTaskAdmissionError::DependencyCycle);
                auto& dependents = m_slabs.payload(dependencyIndex).dependents;
                ContainerDetail::ReserveGrowingCapacity(dependents, AddSize(dependents.size(), static_cast<usize>(1u)));
            }
        }
        else if(dependencyCount > 1u){
            m_searchStack.clear();
            m_searchStack.reserve(dependencyCount);
            for(usize depIdx = 0u; depIdx < dependencyCount; ++depIdx){
                if(resolveLocked(dependencies[depIdx])){
                    const u32 dependencyIndex = dependencies[depIdx].index;
                    if(dependencySearchGeneration != 0u && m_searchVisits[dependencyIndex] == dependencySearchGeneration)
                        return MakeUnexpected(CpuTaskAdmissionError::DependencyCycle);
                    m_searchStack.push_back(dependencyIndex);
                }
            }
            Sort(m_searchStack.begin(), m_searchStack.end());
            for(usize first = 0u; first < m_searchStack.size();){
                usize last = first + 1u;
                while(last < m_searchStack.size() && m_searchStack[last] == m_searchStack[first])
                    ++last;
                auto& dependents = m_slabs.payload(m_searchStack[first]).dependents;
                ContainerDetail::ReserveGrowingCapacity(dependents, AddSize(dependents.size(), last - first));
                first = last;
            }
        }
        meta.scope = scope;
        meta.cost = options.cost;
        meta.priority = options.priority;
        meta.target = options.target;
        meta.parent = {};
        meta.dependencies = 0u;
        meta.children = 0u;
        meta.canceled = false;
        meta.hasHandleWaiter = false;
        meta.hasDependents = false;
        meta.next = TaskHandle::s_InvalidIndex;
        if(inScheduler){
            if(TaskMeta* const parent = resolveLocked(activeExec->task)){
                meta.parent = activeExec->task;
                ++parent->children;
                s_Execution->spawnedChildren = true;
                meta.canceled = parent->canceled;
            }
        }
        for(usize depIdx = 0u; depIdx < dependencyCount; ++depIdx){
            if(TaskMeta* const dependency = resolveLocked(dependencies[depIdx])){
                const u32 dependencyIndex = dependencies[depIdx].index;
                dependency->hasDependents = true;
                m_slabs.payload(dependencyIndex).dependents.push_back(handle);
                ++meta.dependencies;
                meta.canceled = meta.canceled || dependency->canceled;
            }
            else
                meta.canceled = meta.canceled || wasCanceledLocked(dependencies[depIdx]);
        }
        payload.function = Move(function);
        payload.profileLabel = options.profileLabel;
        release.release();
        if(m_outstanding++ == 0u)
            m_outstandingAtomic.store(1u, MemoryOrder::release);
        m_statistics.peakOutstandingTasks = Max(m_statistics.peakOutstandingTasks, m_outstanding);
        if(scope)
            scope->m_pending.fetch_add(1u, MemoryOrder::release);
        if(m_scopeSearchActive)
            invalidateScopeSearchLocked(true);
        meta.state = TaskState::Waiting;
        if(meta.dependencies == 0u)
            enqueueLocked(handle.index, meta);
        takeWorkerWakePlanLocked(wakePlan);
        wakeJoiners = publishJoinWakeLocked(true);
    }
    notifyProgress(wakePlan, wakeJoiners);
    return handle;
}

CpuTaskScheduler::TaskMeta* CpuTaskScheduler::resolveLocked(const TaskHandle handle)noexcept{
    if(!handle.valid() || handle.domainIdentity != m_domainIdentity || handle.index >= m_slabs.nodeCount)
        return nullptr;
    TaskMeta& meta = m_slabs.meta(handle.index);
    return meta.generation == handle.generation && meta.state != TaskState::Free ? &meta : nullptr;
}

const CpuTaskScheduler::TaskMeta* CpuTaskScheduler::resolveLocked(const TaskHandle handle)const noexcept{
    if(!handle.valid() || handle.domainIdentity != m_domainIdentity || handle.index >= m_slabs.nodeCount)
        return nullptr;
    const TaskMeta& meta = m_slabs.meta(handle.index);
    return meta.generation == handle.generation && meta.state != TaskState::Free ? &meta : nullptr;
}

b8 CpuTaskScheduler::wasCanceledLocked(const TaskHandle handle)const noexcept{
    if(!handle.valid() || handle.domainIdentity != m_domainIdentity || handle.index >= m_slabs.nodeCount)
        return false;
    const TaskMeta& meta = m_slabs.meta(handle.index);
    if(handle.generation == meta.latestCanceledGeneration)
        return true;
    const auto& olderCanceledGenerations = m_slabs.payload(handle.index).olderCanceledGenerations;
    const auto found = LowerBound(olderCanceledGenerations.begin(), olderCanceledGenerations.end(), handle.generation);
    return found != olderCanceledGenerations.end() && *found == handle.generation;
}

b8 CpuTaskScheduler::isScopeCanceledLocked(const CpuTaskScope* scope)const noexcept{
    while(scope){
        if(scope->m_canceled.load(MemoryOrder::relaxed))
            return true;
        scope = scope->m_parentScope;
    }
    return false;
}

b8 CpuTaskScheduler::isRangeCanceled(const CpuTaskScope* scope)const noexcept{
    if(m_abortingAtomic.load(MemoryOrder::acquire))
        return true;
    while(scope){
        if(scope->m_canceled.load(MemoryOrder::acquire))
            return true;
        scope = scope->m_parentScope;
    }
    return false;
}

void CpuTaskScheduler::enqueueLocked(const u32 index, TaskMeta& meta)noexcept{
    const usize queueIndex = QueueIndex(meta.cost, meta.priority, meta.target);
    ReadyQueue& queue = m_ready[queueIndex];
    m_nonEmptyReadyMask |= static_cast<u16>(1u << queueIndex);
    meta.state = TaskState::Ready;
    if(m_profileEnabled.load(MemoryOrder::relaxed))
        profileReadyLocked(index);
    if(meta.target == CpuTaskTarget::Worker)
        ++m_readyWorkerCosts[meta.cost];
    meta.next = TaskHandle::s_InvalidIndex;
    if(queue.tail != TaskHandle::s_InvalidIndex)
        m_slabs.meta(queue.tail).next = index;
    else
        queue.head = index;
    queue.tail = index;
}

u16 CpuTaskScheduler::eligibleQueueMaskLocked(const CpuAffinity::Enum affinity, const b8 mainThread, const b8 executing, const b8 cooperative)const noexcept{
    u16 costMask = mainThread ? static_cast<u16>(1u << s_MainThreadTargetSlot) : 0u;
    if(!(mainThread && m_workerCount != 0u && !cooperative) && (mainThread || executing)){
        costMask |= static_cast<u16>(1u << CpuTaskCost::Any);
        if(cooperative || affinity == CpuAffinity::Any || affinity == CpuAffinity::Performance || m_busyPerformance >= m_statistics.performanceWorkers)
            costMask |= static_cast<u16>(1u << CpuTaskCost::Heavy);
        if(cooperative || affinity == CpuAffinity::Any || affinity == CpuAffinity::Efficiency || m_busyEfficiency >= m_statistics.efficiencyWorkers)
            costMask |= static_cast<u16>(1u << CpuTaskCost::Light);
    }
    return static_cast<u16>(costMask | (costMask << s_CostSlotCount) | (costMask << (2u * s_CostSlotCount)));
}

u32 CpuTaskScheduler::claimTaskLocked(const u16 candidateMask, const CpuAffinity::Enum affinity, ScopeWait* const preferredScope, TaskMeta*& outMeta)noexcept{
    static constexpr u64 s_BackgroundAdmissionPeriod = __hidden_cpu_task_scheduler::s_BackgroundAdmissionPeriod;
    static constexpr u64 s_BackgroundAdmissionTick = __hidden_cpu_task_scheduler::s_BackgroundAdmissionTick;
    static constexpr u64 s_NormalAdmissionPeriod = __hidden_cpu_task_scheduler::s_NormalAdmissionPeriod;
    static constexpr u64 s_NormalAdmissionTick = __hidden_cpu_task_scheduler::s_NormalAdmissionTick;
    static constexpr Array<Array<u8, 3u>, 3u> s_PriorityOrder = {{
        {{ 0u, 1u, 2u }},
        {{ 1u, 0u, 2u }},
        {{ 2u, 0u, 1u }},
    }};
    static constexpr Array<Array<u8, 16u>, 2u> s_BestCostSlot = {{
        {{ 255u, 0u, 1u, 1u, 2u, 0u, 1u, 1u, 3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u }},
        {{ 255u, 0u, 1u, 0u, 2u, 2u, 2u, 2u, 3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u }},
    }};

    const u64 dispatch = m_dispatchCount;
    const usize firstPriority = dispatch % s_BackgroundAdmissionPeriod == s_BackgroundAdmissionTick
        ? static_cast<usize>(CpuTaskPriority::Background)
        : (dispatch % s_NormalAdmissionPeriod == s_NormalAdmissionTick ? static_cast<usize>(CpuTaskPriority::Normal) : static_cast<usize>(CpuTaskPriority::Critical))
    ;
    const auto& priorityOrder = s_PriorityOrder[firstPriority];

    if(!preferredScope){
        const usize effRow = affinity == CpuAffinity::Efficiency ? 1u : 0u;
        for(usize pass = 0u; pass < s_PrioritySlotCount; ++pass){
            const usize priority = priorityOrder[pass];
            const u16 priorityBits = static_cast<u16>((candidateMask >> (priority * s_CostSlotCount)) & 0xFu);
            if(priorityBits == 0u)
                continue;
            const usize cost = s_BestCostSlot[effRow][priorityBits];
            const usize index = priority * s_CostSlotCount + cost;
            ReadyQueue& queue = m_ready[index];
            const u32 nodeIndex = queue.head;
            FOUNDATION_ASSERT(nodeIndex != TaskHandle::s_InvalidIndex);
            TaskMeta& meta = m_slabs.meta(nodeIndex);
            queue.head = meta.next;
            if(queue.tail == nodeIndex)
                queue.tail = TaskHandle::s_InvalidIndex;
            if(queue.head == TaskHandle::s_InvalidIndex)
                m_nonEmptyReadyMask &= static_cast<u16>(~(1u << index));
            meta.next = TaskHandle::s_InvalidIndex;
            meta.state = TaskState::Running;
            if(meta.target == CpuTaskTarget::Worker)
                --m_readyWorkerCosts[meta.cost];
            ++m_dispatchCount;
            outMeta = &meta;
            return nodeIndex;
        }
        outMeta = nullptr;
        return TaskHandle::s_InvalidIndex;
    }

    const usize preferredCost = affinity == CpuAffinity::Efficiency ? CpuTaskCost::Light : CpuTaskCost::Heavy;
    const Array<usize, s_CostSlotCount> costs = { s_MainThreadTargetSlot, preferredCost, 0u, preferredCost == CpuTaskCost::Heavy ? static_cast<usize>(CpuTaskCost::Light) : static_cast<usize>(CpuTaskCost::Heavy) };
    for(usize pass = 0u; pass < s_PrioritySlotCount; ++pass){
        const usize priority = priorityOrder[pass];
        const u16 priorityBits = static_cast<u16>((candidateMask >> (priority * s_CostSlotCount)) & 0xFu);
        if(priorityBits == 0u)
            continue;
        for(const usize cost : costs){
            if((priorityBits & static_cast<u16>(1u << cost)) == 0u)
                continue;
            const usize index = priority * s_CostSlotCount + cost;
            ReadyQueue& queue = m_ready[index];
            FOUNDATION_ASSERT(queue.head != TaskHandle::s_InvalidIndex);
            u32 previous = TaskHandle::s_InvalidIndex;
            const u32 nodeIndex = findScopeReadyLocked(index, *preferredScope, previous);
            if(nodeIndex == TaskHandle::s_InvalidIndex)
                continue;
            TaskMeta& meta = m_slabs.meta(nodeIndex);
            const u32 nextNode = meta.next;
            if(previous == TaskHandle::s_InvalidIndex)
                queue.head = nextNode;
            else
                m_slabs.meta(previous).next = nextNode;
            if(queue.tail == nodeIndex)
                queue.tail = previous;
            if(queue.head == TaskHandle::s_InvalidIndex)
                m_nonEmptyReadyMask &= static_cast<u16>(~(1u << index));
            meta.next = TaskHandle::s_InvalidIndex;
            meta.state = TaskState::Running;
            if(meta.target == CpuTaskTarget::Worker)
                --m_readyWorkerCosts[meta.cost];
            ++m_dispatchCount;
            outMeta = &meta;
            return nodeIndex;
        }
    }
    outMeta = nullptr;
    return TaskHandle::s_InvalidIndex;
}

void CpuTaskScheduler::unlinkRangeGroupLocked(ParallelRangeGroup& group)noexcept{
    if(!group.linked)
        return;
    ParallelRangeGroup** cursor = &m_activeRangeHead;
    while(*cursor){
        if(*cursor == &group){
            *cursor = group.next;
            group.next = nullptr;
            group.linked = false;
            return;
        }
        cursor = &((*cursor)->next);
    }
    group.next = nullptr;
    group.linked = false;
}

void CpuTaskScheduler::waitRangeHelpers(ParallelRangeGroup& group)noexcept{
    u32 state = group.activeHelpers.load(MemoryOrder::acquire);
    while((state & s_RangeHelperCountMask) != 0u){
        for(u32 spin = 0u; spin < s_JoinSpinIterations; ++spin){
            state = group.activeHelpers.load(MemoryOrder::acquire);
            if((state & s_RangeHelperCountMask) == 0u)
                return;
            MachinePause(1);
        }
        const u32 epoch = m_rangeHelperEpoch.load(MemoryOrder::acquire);
        state = group.activeHelpers.load(MemoryOrder::acquire);
        while((state & s_RangeHelperCountMask) != 0u && (state & s_RangeWaiterParkedBit) == 0u){
            if(group.activeHelpers.compare_exchange_weak(state, state | s_RangeWaiterParkedBit, MemoryOrder::acq_rel, MemoryOrder::acquire)){
                state |= s_RangeWaiterParkedBit;
                break;
            }
        }
        if((state & s_RangeHelperCountMask) == 0u)
            return;
        m_rangeHelperEpoch.wait(epoch, MemoryOrder::acquire);
        state = group.activeHelpers.load(MemoryOrder::acquire);
    }
}

CpuTaskScheduler::ParallelRangeGroup* CpuTaskScheduler::claimRangeLocked(const u16 eligibleMask, ScopeWait* const preferredScope)noexcept{
    ParallelRangeGroup** cursor = &m_activeRangeHead;
    while(*cursor){
        ParallelRangeGroup* const group = *cursor;
        if(group->remainingHelperClaims == 0u || group->nextChunk.load(MemoryOrder::relaxed) >= group->chunkCount){
            *cursor = group->next;
            group->next = nullptr;
            group->linked = false;
            continue;
        }
        const u16 costBit = static_cast<u16>(1u << static_cast<usize>(group->options.cost));
        if((eligibleMask & costBit) != 0u){
            if(!preferredScope || group->scope == &preferredScope->scope || (!group->fastUnscoped && contributesToScopeLocked(group->anchorHandle.index, *preferredScope))){
                --group->remainingHelperClaims;
                if(group->remainingHelperClaims == 0u){
                    *cursor = group->next;
                    group->next = nullptr;
                    group->linked = false;
                }
                group->activeHelpers.fetch_add(1u, MemoryOrder::acq_rel);
                return group;
            }
        }
        cursor = &group->next;
    }
    return nullptr;
}

b8 CpuTaskScheduler::claimNextLocked(const CpuAffinity::Enum affinity, const b8 mainThread, const b8 executing, const b8 cooperative, ScopeWait* const preferredScope, const usize workerIndex, ClaimedExecution& outClaim, const b8 countDepth)noexcept{
    const u16 eligibleMask = eligibleQueueMaskLocked(affinity, mainThread, executing, cooperative);
    const u16 candidateMask = static_cast<u16>(m_nonEmptyReadyMask & eligibleMask);
    if(candidateMask != 0u){
        TaskMeta* meta = nullptr;
        const u32 nodeIndex = claimTaskLocked(candidateMask, affinity, preferredScope, meta);
        if(meta){
            beginClaimedLocked(nodeIndex, *meta, workerIndex, affinity, cooperative, countDepth, outClaim, countDepth);
            return true;
        }
    }
    if(m_activeRangeHead){
        if(ParallelRangeGroup* const range = claimRangeLocked(eligibleMask, preferredScope)){
            outClaim.rangeGroup = range;
            outClaim.handle = range->anchorHandle;
            outClaim.meta = nullptr;
            outClaim.payload = nullptr;
            outClaim.invoke = true;
            b8 reachedSaturation = false;
            if(countDepth)
                addActiveDepthLocked(workerIndex, affinity, reachedSaturation);
            if(cooperative)
                ++m_statistics.cooperativeTasks;
            switch(affinity){
            case CpuAffinity::Performance: ++m_statistics.performanceTasks; break;
            case CpuAffinity::Efficiency: ++m_statistics.efficiencyTasks; break;
            default: ++m_statistics.unclassifiedTasks; break;
            }
            outClaim.wakePlan.parkedCount = 0u;
            outClaim.wakePlan.overflowWakeAll = false;
            if(reachedSaturation)
                takeWorkerWakePlanLocked(outClaim.wakePlan);
            takeRangeWakePlanLocked(*range, 2u, outClaim.wakePlan);
            return true;
        }
    }
    return false;
}

b8 CpuTaskScheduler::hasReadyLocked(const CpuAffinity::Enum affinity, const b8 mainThread, const b8 executing, const b8 cooperative, ScopeWait* const preferredScope)noexcept{
    const u16 eligibleMask = eligibleQueueMaskLocked(affinity, mainThread, executing, cooperative);
    const u16 candidateMask = static_cast<u16>(m_nonEmptyReadyMask & eligibleMask);
    if(candidateMask != 0u){
        if(!preferredScope)
            return true;
        for(u32 bits = candidateMask; bits != 0u; bits &= bits - 1u){
            const usize index = static_cast<usize>(CountTrailingZeros(bits));
            u32 previous = TaskHandle::s_InvalidIndex;
            if(findScopeReadyLocked(index, *preferredScope, previous) != TaskHandle::s_InvalidIndex)
                return true;
        }
    }
    for(ParallelRangeGroup* group = m_activeRangeHead; group; group = group->next){
        if(group->remainingHelperClaims == 0u || group->nextChunk.load(MemoryOrder::relaxed) >= group->chunkCount)
            continue;
        const u16 costBit = static_cast<u16>(1u << static_cast<usize>(group->options.cost));
        if((eligibleMask & costBit) != 0u){
            if(!preferredScope || group->scope == &preferredScope->scope || (!group->fastUnscoped && contributesToScopeLocked(group->anchorHandle.index, *preferredScope)))
                return true;
        }
    }
    return false;
}

void CpuTaskScheduler::addActiveDepthLocked(const usize workerIndex, const CpuAffinity::Enum affinity, b8& outReachedSaturation)noexcept{
    outReachedSaturation = false;
    if(workerIndex != 0u && m_workerDepth[workerIndex - 1u]++ == 0u){
        if(affinity == CpuAffinity::Performance){
            ++m_busyPerformance;
            outReachedSaturation = m_busyPerformance == m_statistics.performanceWorkers;
        }
        else if(affinity == CpuAffinity::Efficiency){
            ++m_busyEfficiency;
            outReachedSaturation = m_busyEfficiency == m_statistics.efficiencyWorkers;
        }
    }
}

void CpuTaskScheduler::removeActiveDepthLocked(const usize workerIndex, const CpuAffinity::Enum affinity)noexcept{
    if(workerIndex != 0u && --m_workerDepth[workerIndex - 1u] == 0u){
        if(affinity == CpuAffinity::Performance)
            --m_busyPerformance;
        else if(affinity == CpuAffinity::Efficiency)
            --m_busyEfficiency;
    }
}

void CpuTaskScheduler::beginClaimedLocked(const u32 index, TaskMeta& meta, const usize workerIndex, const CpuAffinity::Enum affinity, const b8 cooperative, const b8 computeWakePlan, ClaimedExecution& outClaim, const b8 countDepth)noexcept{
    const TaskHandle handle{ m_domainIdentity, index, meta.generation };
    outClaim.rangeGroup = nullptr;
    outClaim.handle = handle;
    outClaim.meta = &meta;
    outClaim.payload = &m_slabs.payload(index);

    b8 canceled = meta.canceled || m_aborting || isScopeCanceledLocked(meta.scope);
    if(!canceled && meta.parent.valid()){
        TaskHandle ancestor = meta.parent;
        while(TaskMeta* const parent = resolveLocked(ancestor)){
            if(parent->canceled || isScopeCanceledLocked(parent->scope)){
                canceled = true;
                break;
            }
            ancestor = parent->parent;
        }
    }
    meta.canceled = canceled;

    b8 reachedAffinitySaturation = false;
    if(countDepth)
        addActiveDepthLocked(workerIndex, affinity, reachedAffinitySaturation);
    outClaim.invoke = !canceled;
    if(cooperative)
        ++m_statistics.cooperativeTasks;
    if(outClaim.invoke){
        switch(affinity){
        case CpuAffinity::Performance: ++m_statistics.performanceTasks; break;
        case CpuAffinity::Efficiency: ++m_statistics.efficiencyTasks; break;
        default: ++m_statistics.unclassifiedTasks; break;
        }
    }
    if(outClaim.invoke && m_profileEnabled.load(MemoryOrder::relaxed)){
        outClaim.profile = prepareProfileLocked(CpuTaskProfileKind::Execution, handle, {}, workerIndex, affinity);
        if(index < m_readyProfiles.size()){
            const ReadyProfile& ready = m_readyProfiles[index];
            if(ready.captureEpoch == m_profileEpoch)
                outClaim.readyProfile = ready;
        }
    }
    outClaim.wakePlan.parkedCount = 0u;
    outClaim.wakePlan.overflowWakeAll = false;
    if(computeWakePlan && reachedAffinitySaturation)
        takeWorkerWakePlanLocked(outClaim.wakePlan);
}

void CpuTaskScheduler::executeClaimed(ClaimedExecution& claimed, const usize workerIndex, const CpuAffinity::Enum affinity, ClaimedExecution* const nextClaim, const ClaimPipeline* const pipeline){
    if(claimed.rangeGroup){
        ParallelRangeGroup* const rangeGroup = claimed.rangeGroup;
        const TaskHandle handle = claimed.handle;
        notifyWorkers(claimed.wakePlan);
        b8 succeeded = false;
        b8 spawnedChildren = false;
        ScopeExit finish([&]()noexcept{
            WorkerWakePlan wakePlan;
            b8 wakeJoiners = false;
            {
                ScopedLock lock(m_mutex);
                if(!succeeded){
                    m_aborting = true;
                    m_abortingAtomic.store(true, MemoryOrder::release);
                    if(TaskMeta* const anchorMeta = resolveLocked(handle))
                        anchorMeta->canceled = true;
                }
                const b8 profileOn = m_profileEnabled.load(MemoryOrder::relaxed);
                if(nextClaim && succeeded && pipeline && !m_stopping){
                    const b8 keepRunning =
                        (!pipeline->stopOnCompleteHandle || resolveLocked(*pipeline->stopOnCompleteHandle) != nullptr)
                        && (!pipeline->stopOnCompleteScope || pipeline->stopOnCompleteScope->m_pending.load(MemoryOrder::acquire) != 0u)
                        && (!pipeline->stopWhenNoOutstanding || m_outstanding != 0u || m_activeFastRanges.load(MemoryOrder::acquire) != 0u);
                    if(keepRunning)
                        (void)claimNextLocked(affinity, pipeline->mainThread, pipeline->executing, pipeline->cooperative, pipeline->preferredScope, workerIndex, *nextClaim, false);
                }
                if(!nextClaim || !nextClaim->hasWork())
                    removeActiveDepthLocked(workerIndex, affinity);
                if(!succeeded){
                    takeWorkerWakePlanLocked(wakePlan);
                    wakeJoiners = publishJoinWakeLocked(true);
                }
                if(nextClaim && !nextClaim->hasWork() && pipeline && pipeline->allowPark && !m_stopping && !profileOn){
                    pushIdleWorkerLocked(workerIndex - 1u, affinity);
                    m_slots[workerIndex - 1u].state.store(s_WorkerSpinning, MemoryOrder::release);
                    nextClaim->parkedFromExecute = true;
                }
            }
            if(!succeeded)
                notifyProgress(wakePlan, wakeJoiners);
            const u32 prev = rangeGroup->activeHelpers.fetch_sub(1u, MemoryOrder::acq_rel);
            if((prev & s_RangeHelperCountMask) == 1u && (prev & s_RangeWaiterParkedBit) != 0u){
                m_rangeHelperEpoch.fetch_add(1u, MemoryOrder::release);
                m_rangeHelperEpoch.notify_all();
            }
        });
        {
            Execution execution(*this, handle, workerIndex, affinity, &spawnedChildren, rangeGroup);
            const NotNull<const void*> rangeContext = rangeGroup->context;
            const NotNull<RangeFunction> rangeInvoke = rangeGroup->invoke;
            const usize rangeBegin = rangeGroup->begin;
            const usize rangeChunkCount = rangeGroup->chunkCount;
            const usize rangeChunkSize = rangeGroup->chunkSize;
            const usize rangeRemainder = rangeGroup->remainder;
            CpuTaskScope* const rangeScope = rangeGroup->scope;
            for(;;){
                if(isRangeCanceled(rangeScope) || rangeGroup->nextChunk.load(MemoryOrder::relaxed) >= rangeChunkCount)
                    break;
                const usize chunk = rangeGroup->nextChunk.fetch_add(1u, MemoryOrder::relaxed);
                if(chunk >= rangeChunkCount)
                    break;
                const BalancedPartitionRange range = ComputeBalancedPartitionRange(rangeBegin, chunk, rangeChunkSize, rangeRemainder);
                rangeInvoke.get()(rangeContext, range.first, range.last);
            }
        }
        succeeded = true;
        return;
    }

    if(!claimed.meta)
        return;
    TaskMeta& meta = *claimed.meta;
    TaskPayload& payload = *claimed.payload;
    const TaskHandle handle = claimed.handle;
    notifyWorkers(claimed.wakePlan);
    b8 succeeded = false;
    b8 spawnedChildren = false;
    ScopeExit finish([&]()noexcept{
        const Timer end = claimed.profile ? TimerNow() : Timer{};
        TaskHandle parentHandle;
        TaskMeta* parentToRetire = nullptr;
        TaskPayload* parentPayload = nullptr;
        WorkerWakePlan wakePlan;
        b8 wakeJoiners = false;
        b8 readyToRetire = false;
        if(!spawnedChildren)
            payload.function.reset();
        {
            ScopedLock lock(m_mutex);
            const b8 profileOn = m_profileEnabled.load(MemoryOrder::relaxed);
            if(claimed.profile){
                if(claimed.readyProfile){
                    ProfileSample queued = *claimed.profile;
                    queued.kind = CpuTaskProfileKind::QueueDelay;
                    queued.begin = claimed.readyProfile->ready;
                    queued.frameIndex = claimed.readyProfile->frameIndex;
                    finishProfileLocked(queued, claimed.profile->begin);
                }
                finishProfileLocked(*claimed.profile, end);
            }
            if(!succeeded){
                m_aborting = true;
                m_abortingAtomic.store(true, MemoryOrder::release);
                meta.canceled = true;
            }
            FOUNDATION_ASSERT(meta.state == TaskState::Running);
            if(!spawnedChildren){
                FOUNDATION_ASSERT(meta.children == 0u);
                meta.state = TaskState::Retiring;
                b8 madeReady = false;
                b8 scopeCompleted = false;
                b8 handleWaited = false;
                retireNodeLocked(handle, meta, parentHandle, parentToRetire, parentPayload, madeReady, scopeCompleted, handleWaited);
                if(nextClaim && succeeded && !parentToRetire && pipeline && !m_stopping){
                    const b8 keepRunning =
                        (!pipeline->stopOnCompleteHandle || resolveLocked(*pipeline->stopOnCompleteHandle) != nullptr)
                        && (!pipeline->stopOnCompleteScope || pipeline->stopOnCompleteScope->m_pending.load(MemoryOrder::acquire) != 0u)
                        && (!pipeline->stopWhenNoOutstanding || m_outstanding != 0u || m_activeFastRanges.load(MemoryOrder::acquire) != 0u);
                    if(keepRunning)
                        (void)claimNextLocked(affinity, pipeline->mainThread, pipeline->executing, pipeline->cooperative, pipeline->preferredScope, workerIndex, *nextClaim, false);
                }
                if(!nextClaim || !nextClaim->hasWork())
                    removeActiveDepthLocked(workerIndex, affinity);
                if(m_totalIdleWorkers != 0u && m_nonEmptyReadyMask != 0u && (madeReady || !succeeded || !pipeline || pipeline->preferredScope != nullptr))
                    takeWorkerWakePlanLocked(wakePlan);
                wakeJoiners = publishJoinWakeLocked(madeReady || scopeCompleted || m_outstanding == 0u || handleWaited || !succeeded);
                if(nextClaim && !nextClaim->hasWork() && !parentToRetire && pipeline && pipeline->allowPark && !m_stopping && !profileOn){
                    pushIdleWorkerLocked(workerIndex - 1u, affinity);
                    m_slots[workerIndex - 1u].state.store(s_WorkerSpinning, MemoryOrder::release);
                    nextClaim->parkedFromExecute = true;
                }
            }
            else{
                removeActiveDepthLocked(workerIndex, affinity);
                readyToRetire = meta.children == 0u;
                meta.state = readyToRetire ? TaskState::Retiring : TaskState::Children;
            }
        }
        if(!spawnedChildren){
            notifyProgress(wakePlan, wakeJoiners);
            if(parentToRetire)
                retire(parentHandle, *parentToRetire, *parentPayload);
        }
        else if(readyToRetire)
            retire(handle, meta, payload);
    });
    {
        Execution execution(*this, handle, workerIndex, affinity, &spawnedChildren);
        if(claimed.profile)
            claimed.profile->begin = TimerNow();
        if(claimed.invoke)
            payload.function();
    }
    succeeded = true;
}

void CpuTaskScheduler::retireNodeLocked(const TaskHandle handle, TaskMeta& meta, TaskHandle& outParentHandle, TaskMeta*& outParentToRetire, TaskPayload*& outParentPayload, b8& outMadeReady, b8& outScopeCompleted, b8& outHandleWaited)noexcept{
    outParentHandle = {};
    outParentToRetire = nullptr;
    outParentPayload = nullptr;
    meta.canceled = meta.canceled || m_aborting || isScopeCanceledLocked(meta.scope);
    outMadeReady = false;
    if(meta.hasDependents){
        auto& dependents = m_slabs.payload(handle.index).dependents;
        for(const TaskHandle dependentHandle : dependents){
            TaskMeta* const dependent = resolveLocked(dependentHandle);
            if(!dependent)
                continue;
            dependent->canceled = dependent->canceled || meta.canceled;
            FOUNDATION_ASSERT(dependent->dependencies > 0u);
            if(--dependent->dependencies == 0u){
                enqueueLocked(dependentHandle.index, *dependent);
                outMadeReady = true;
            }
        }
        dependents.clear();
        meta.hasDependents = false;
    }
    if(TaskMeta* const parent = resolveLocked(meta.parent)){
        parent->canceled = parent->canceled || meta.canceled;
        FOUNDATION_ASSERT(parent->children > 0u);
        if(--parent->children == 0u && parent->state == TaskState::Children){
            parent->state = TaskState::Retiring;
            outParentHandle = meta.parent;
            outParentToRetire = parent;
            outParentPayload = &m_slabs.payload(meta.parent.index);
        }
    }
    outScopeCompleted = meta.scope && meta.scope->m_pending.fetch_sub(1u, MemoryOrder::acq_rel) == 1u;
    outHandleWaited = meta.hasHandleWaiter;
    meta.hasHandleWaiter = false;
    if(meta.canceled){
        if(meta.latestCanceledGeneration != 0u)
            m_slabs.payload(handle.index).olderCanceledGenerations.push_back(meta.latestCanceledGeneration);
        meta.latestCanceledGeneration = handle.generation;
        ++m_statistics.canceledTasks;
    }
    else
        ++m_statistics.completedTasks;
    FOUNDATION_ASSERT(m_outstanding > 0u);
    meta.scope = nullptr;
    meta.parent = {};
    meta.state = TaskState::Free;
    if(meta.generation != Limit<u32>::s_Max){
        ++meta.generation;
        meta.next = m_freeNode;
        m_freeNode = handle.index;
    }
    if(--m_outstanding == 0u){
        m_outstandingAtomic.store(0u, MemoryOrder::release);
        if(m_scopeSearchActive){
            m_scopeSearchActive = false;
            invalidateScopeSearchLocked(true);
        }
    }
}

void CpuTaskScheduler::retire(TaskHandle handle, TaskMeta& first, TaskPayload& firstPayload)noexcept{
    TaskMeta* meta = &first;
    TaskPayload* payload = &firstPayload;
    while(meta){
        FOUNDATION_ASSERT(meta->state == TaskState::Retiring);
        payload->function.reset();
        TaskHandle parentHandle;
        TaskMeta* parentToRetire = nullptr;
        TaskPayload* parentPayload = nullptr;
        WorkerWakePlan wakePlan;
        b8 wakeJoiners = false;
        b8 madeReady = false;
        b8 scopeCompleted = false;
        b8 handleWaited = false;
        {
            ScopedLock lock(m_mutex);
            retireNodeLocked(handle, *meta, parentHandle, parentToRetire, parentPayload, madeReady, scopeCompleted, handleWaited);
            takeWorkerWakePlanLocked(wakePlan);
            wakeJoiners = publishJoinWakeLocked(madeReady || scopeCompleted || m_outstanding == 0u || handleWaited);
        }
        notifyProgress(wakePlan, wakeJoiners);
        handle = parentHandle;
        meta = parentToRetire;
        payload = parentPayload;
    }
}

void CpuTaskScheduler::waitWorkerSlot(const usize slotIndex)noexcept{
    Atomic<u32>& slotState = m_slots[slotIndex].state;
    if(slotState.load(MemoryOrder::acquire) == s_WorkerSpinning){
        for(u32 spin = 0u; spin < s_WorkerSpinIterations; ++spin){
            if(slotState.load(MemoryOrder::relaxed) != s_WorkerSpinning)
                break;
            MachinePause(1);
        }
        if(slotState.load(MemoryOrder::acquire) == s_WorkerSpinning && !m_stoppingAtomic.load(MemoryOrder::acquire)){
            u32 expected = s_WorkerSpinning;
            slotState.compare_exchange_strong(expected, s_WorkerParked, MemoryOrder::acq_rel, MemoryOrder::acquire);
        }
    }
    while(slotState.load(MemoryOrder::acquire) == s_WorkerParked){
        if(m_stoppingAtomic.load(MemoryOrder::acquire))
            return;
        slotState.wait(s_WorkerParked, MemoryOrder::acquire);
    }
}

void CpuTaskScheduler::workerLoop(const StopToken& stop, const usize workerIndex){
    const usize slotIndex = workerIndex - 1u;
    const CpuWorkerPlacement placement = m_placements[slotIndex];
    CpuAffinity::Enum affinity = placement.affinity;
    if(placement.valid() && !SetCurrentThreadCpuPlacement(placement)){
        ScopedLock lock(m_mutex);
        ++m_statistics.placementFailures;
        if(affinity == CpuAffinity::Performance)
            --m_statistics.performanceWorkers;
        else if(affinity == CpuAffinity::Efficiency)
            --m_statistics.efficiencyWorkers;
        if(affinity != CpuAffinity::Any)
            ++m_statistics.unclassifiedWorkers;
        affinity = CpuAffinity::Any;
    }
    std::stop_callback stopCallback(stop, [this, slotIndex]()noexcept{
        m_stoppingAtomic.store(true, MemoryOrder::release);
        {
            NothrowScopedLock lock(m_mutex);
            m_stopping = true;
            if(m_joinWaiters != 0u)
                m_joinEpoch.fetch_add(1u, MemoryOrder::release);
        }
        m_joinEpoch.notify_all();
        if(m_slots[slotIndex].state.exchange(s_WorkerNotified, MemoryOrder::acq_rel) == s_WorkerParked)
            m_slots[slotIndex].state.notify_one();
    });
    if(stop.stop_requested())
        return;

    const ClaimPipeline pipeline{
        .preferredScope = nullptr,
        .stopOnCompleteHandle = nullptr,
        .stopOnCompleteScope = nullptr,
        .stopWhenNoOutstanding = false,
        .mainThread = false,
        .executing = true,
        .cooperative = false,
        .allowPark = true,
    };
    Execution worker(*this, {}, workerIndex, affinity);
    ClaimedExecution claims[2];
    u32 active = 0u;
    for(;;){
        ClaimedExecution& claimed = claims[active];
        if(claimed.parkedFromExecute){
            claimed.parkedFromExecute = false;
            waitWorkerSlot(slotIndex);
            if(m_stoppingAtomic.load(MemoryOrder::acquire) || stop.stop_requested())
                return;
        }
        if(!claimed.hasWork()){
            Optional<ProfileSample> idle;
            b8 waited = false;
            {
                UniqueLock lock(m_mutex);
                if(m_stopping || stop.stop_requested())
                    return;
                if(m_idleNext[slotIndex] == s_WorkerWakingTask){
                    m_idleNext[slotIndex] = s_WorkerNotIdle;
                    FOUNDATION_ASSERT(m_wakingWorkers[affinity] > 0u);
                    --m_wakingWorkers[affinity];
                }
                while(!claimNextLocked(affinity, false, true, false, nullptr, workerIndex, claimed)){
                    const b8 profileOn = m_profileEnabled.load(MemoryOrder::relaxed);
                    if(profileOn && !idle){
                        idle = prepareProfileLocked(CpuTaskProfileKind::WorkerIdle, {}, {}, workerIndex, affinity);
                        idle->begin = TimerNow();
                    }
                    pushIdleWorkerLocked(slotIndex, affinity);
                    m_slots[slotIndex].state.store(profileOn ? s_WorkerParked : s_WorkerSpinning, MemoryOrder::release);
                    waited = true;
                    lock.unlock();
                    waitWorkerSlot(slotIndex);
                    if(m_stoppingAtomic.load(MemoryOrder::acquire) || stop.stop_requested())
                        return;
                    lock.lock();
                    if(m_stopping || stop.stop_requested())
                        return;
                    if(m_idleNext[slotIndex] == s_WorkerWakingTask){
                        m_idleNext[slotIndex] = s_WorkerNotIdle;
                        FOUNDATION_ASSERT(m_wakingWorkers[affinity] > 0u);
                        --m_wakingWorkers[affinity];
                    }
                }
                if(waited && idle)
                    finishProfileLocked(*idle, TimerNow());
            }
        }
        const u32 next = active ^ 1u;
        claims[next].resetFast();
        executeClaimed(claimed, workerIndex, affinity, &claims[next], &pipeline);
        claimed.resetFast();
        active = next;
    }
}

b8 CpuTaskScheduler::executeOne(const b8 cooperative, ScopeWait* const preferredScope){
    ClaimedExecution claimed;
    const Execution* const activeExec = s_Execution;
    const b8 executing = activeExec && &activeExec->scheduler == this;
    const usize workerIndex = executing ? activeExec->workerIndex : 0u;
    const CpuAffinity::Enum affinity = executing ? activeExec->affinity : CpuAffinity::Any;
    const b8 mainThread = isMainThread();
    {
        ScopedLock lock(m_mutex);
        (void)claimNextLocked(affinity, mainThread, executing, cooperative, preferredScope, workerIndex, claimed);
    }
    if(!claimed.hasWork())
        return false;
    executeClaimed(claimed, workerIndex, affinity);
    return true;
}

void CpuTaskScheduler::ensureSearchBuffersLocked(){
    const usize nodeCount = m_slabs.nodeCount;
    if(m_searchVisits.size() < nodeCount)
        m_searchVisits.resize(nodeCount, 0u);
    if(m_scopeContributionVisits.size() < nodeCount)
        m_scopeContributionVisits.resize(nodeCount, 0u);
}

void CpuTaskScheduler::beginLockedSearch(){
    ensureSearchBuffersLocked();
    if(++m_searchGeneration == 0u){
        for(u64& visit : m_searchVisits)
            visit = 0u;
        ++m_searchGeneration;
    }
    m_searchStack.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CpuTaskScope::~CpuTaskScope()noexcept(false){
    if(UncaughtExceptionCount() > 0){
        drain();
        return;
    }
    ScopeExit drainOnFailure([this]()noexcept{ drain(); });

    wait();
    drainOnFailure.release();
}

void CpuTaskScope::drain()noexcept{
    {
        ScopedLock lock(m_scheduler.m_mutex);
        m_scheduler.m_aborting = true;
        m_scheduler.m_abortingAtomic.store(true, MemoryOrder::release);
    }
    m_scheduler.notifyProgress();
    m_scheduler.waitScope(*this);
}

void CpuTaskScope::cancel()noexcept{
    {
        ScopedLock lock(m_scheduler.m_mutex);
        m_canceled.store(true, MemoryOrder::release);
    }
    m_scheduler.notifyProgress();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

