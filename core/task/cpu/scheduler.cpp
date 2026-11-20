// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"
#include "arena_names.h"

#include <global/atomic_identity.h>
#include <global/bit.h>
#include <global/exception.h>


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


CpuTaskScheduler::TaskNode::TaskNode(Alloc::GlobalArena& arena)noexcept(noexcept(TaskFunction{}) && noexcept(Vector<TaskHandle, Alloc::GlobalArena>(arena)) && noexcept(Vector<u32, Alloc::GlobalArena>(arena)))
    : dependents(arena)
    , olderCanceledGenerations(arena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CpuTaskScheduler::Execution::Execution(CpuTaskScheduler& owner, TaskHandle handle, usize index, CpuAffinity::Enum workerAffinity, b8* const spawned)noexcept
    : scheduler(owner)
    , previous(s_Execution)
    , task(handle)
    , workerIndex(index)
    , affinity(workerAffinity)
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
    const usize target = options.target == CpuTaskTarget::MainThread ? s_MainThreadTargetSlot : static_cast<usize>(options.cost);
    return static_cast<usize>(options.priority) * s_CostSlotCount + target;
}


CpuTaskScheduler::CpuTaskScheduler(const u32 workerCount)
    : CpuTaskScheduler(WorkerConfig(workerCount))
{}

CpuTaskScheduler::CpuTaskScheduler(const CpuTaskSchedulerConfig& config)
    : m_domainIdentity(AllocateDomainIdentity())
    , m_arena(TaskArenaScope::s_CpuTaskScheduler)
    , m_nodes(m_arena)
    , m_placements(m_arena)
    , m_workerDepth(m_arena)
    , m_searchStack(m_arena)
    , m_searchVisits(m_arena)
    , m_scopeContributionVisits(m_arena)
    , m_profileBuffer(m_arena, config.profileEventCapacity)
    , m_readyProfiles(m_arena)
    , m_mainThread(QueryCurrentThreadId())
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
    m_workers.reserve(m_workerCount);
    m_workerDepth.resize(m_workerCount, 0u);
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
    while(executeOne(false)){}
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
    result.outstandingTasks = m_outstanding;
    result.profileEnabled = m_profileEnabled.load(MemoryOrder::relaxed);
    result.profilePendingEvents = m_profileBuffer.pending();
    result.profileRecordedEvents = m_profileBuffer.recorded();
    result.profileDroppedEvents = m_profileBuffer.dropped();
    return result;
}


Expected<CpuTaskScheduler::TaskReservation, CpuTaskAdmissionError::Enum> CpuTaskScheduler::reserveTaskLocked(){
    u32 index = m_freeNode;
    if(index == TaskHandle::s_InvalidIndex){
        if(m_nodes.size() >= TaskHandle::s_InvalidIndex)
            return MakeUnexpected(CpuTaskAdmissionError::CapacityExhausted);
        index = static_cast<u32>(m_nodes.size());
        ContainerDetail::ReserveGrowingCapacity(m_searchStack, m_nodes.size() + 1u);
        m_searchVisits.resize(m_nodes.size() + 1u, 0u);
        m_scopeContributionVisits.resize(m_nodes.size() + 1u, 0u);
        if(m_profileEnabled.load(MemoryOrder::relaxed))
            m_readyProfiles.resize(m_nodes.size() + 1u);
        m_nodes.emplace_back(m_arena);
    }
    else
        m_freeNode = m_nodes[index].next;
    TaskNode& node = m_nodes[index];
    node.state = TaskState::Preparing;
    node.next = TaskHandle::s_InvalidIndex;
    return TaskReservation{ TaskHandle{ m_domainIdentity, index, node.generation }, node };
}

Expected<CpuTaskScheduler::TaskHandle, CpuTaskAdmissionError::Enum> CpuTaskScheduler::submitTask(TaskFunction&& function, CpuTaskScope* const scope, const CpuTaskOptions options, const TaskHandle* const dependencies, const usize dependencyCount){
    if(!function)
        return MakeUnexpected(CpuTaskAdmissionError::InvalidFunction);
    if(dependencyCount != 0u && !dependencies)
        return MakeUnexpected(CpuTaskAdmissionError::InvalidDependencies);
    if(options.cost > CpuTaskCost::Light || options.priority > CpuTaskPriority::Background || options.target > CpuTaskTarget::MainThread)
        return MakeUnexpected(CpuTaskAdmissionError::InvalidOptions);

    static_assert(noexcept(DeclVal<TaskNode&>().function = Move(function)));

    WorkerWakePlan wakePlan;
    b8 wakeJoiners = false;
    TaskHandle handle;
    {
        ScopedLock lock(m_mutex);
        if(m_aborting)
            return MakeUnexpected(CpuTaskAdmissionError::Aborting);
        if(isScopeCanceledLocked(scope))
            return MakeUnexpected(CpuTaskAdmissionError::ScopeCanceled);
        for(usize index = 0u; index < dependencyCount; ++index){
            if(dependencies[index].valid() && dependencies[index].domainIdentity != m_domainIdentity)
                return MakeUnexpected(CpuTaskAdmissionError::ForeignDependency);
        }
        const auto reservation = reserveTaskLocked();
        if(!reservation)
            return MakeUnexpected(reservation.error());
        handle = reservation->handle;
        TaskNode& node = reservation->node;
        ScopeExit release([&]()noexcept{
            node.state = TaskState::Free;
            node.next = m_freeNode;
            m_freeNode = handle.index;
        });
        if(node.latestCanceledGeneration != 0u)
            ContainerDetail::ReserveGrowingCapacity(node.olderCanceledGenerations, AddSize(node.olderCanceledGenerations.size(), 1u));
        u64 dependencySearchGeneration = 0u;
        if(dependencyCount != 0u && s_Execution && &s_Execution->scheduler == this && s_Execution->task.valid()){
            beginLockedSearch();
            dependencySearchGeneration = m_searchGeneration;
            const auto visit = [this](const TaskHandle candidate)noexcept{
                if(resolveLocked(candidate) && m_searchVisits[candidate.index] != m_searchGeneration){
                    m_searchVisits[candidate.index] = m_searchGeneration;
                    m_searchStack.push_back(candidate.index);
                }
            };
            visit(s_Execution->task);
            for(usize cursor = 0u; cursor < m_searchStack.size(); ++cursor){
                const u32 candidateIndex = m_searchStack[cursor];
                const TaskNode& reachable = m_nodes[candidateIndex];
                for(const TaskHandle dependent : reachable.dependents)
                    visit(dependent);
                visit(reachable.parent);
            }
        }
        if(dependencyCount == 1u){
            if(resolveLocked(dependencies[0])){
                const u32 dependencyIndex = dependencies[0].index;
                if(dependencySearchGeneration != 0u && m_searchVisits[dependencyIndex] == dependencySearchGeneration)
                    return MakeUnexpected(CpuTaskAdmissionError::DependencyCycle);
                auto& dependents = m_nodes[dependencyIndex].dependents;
                ContainerDetail::ReserveGrowingCapacity(dependents, AddSize(dependents.size(), static_cast<usize>(1u)));
            }
        }
        else if(dependencyCount > 1u){
            m_searchStack.clear();
            m_searchStack.reserve(dependencyCount);
            for(usize index = 0u; index < dependencyCount; ++index){
                if(resolveLocked(dependencies[index])){
                    const u32 dependencyIndex = dependencies[index].index;
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
                auto& dependents = m_nodes[m_searchStack[first]].dependents;
                ContainerDetail::ReserveGrowingCapacity(dependents, AddSize(dependents.size(), last - first));
                first = last;
            }
        }
        node.function = Move(function);
        node.scope = scope;
        node.options = options;
        node.parent = {};
        node.dependencies = 0u;
        node.children = 0u;
        node.canceled = false;
        if(s_Execution && &s_Execution->scheduler == this){
            if(TaskNode* parent = resolveLocked(s_Execution->task)){
                node.parent = s_Execution->task;
                ++parent->children;
                s_Execution->spawnedChildren = true;
                node.canceled = parent->canceled;
            }
        }
        for(usize index = 0u; index < dependencyCount; ++index){
            if(TaskNode* dependency = resolveLocked(dependencies[index])){
                dependency->dependents.push_back(handle);
                ++node.dependencies;
                node.canceled = node.canceled || dependency->canceled;
            }
            else
                node.canceled = node.canceled || wasCanceledLocked(dependencies[index]);
        }
        ++m_outstanding;
        m_statistics.peakOutstandingTasks = Max(m_statistics.peakOutstandingTasks, m_outstanding);
        if(scope)
            scope->m_pending.fetch_add(1u, MemoryOrder::release);
        if(m_scopeSearchActive)
            invalidateScopeSearchLocked(true);
        node.state = TaskState::Waiting;
        if(node.dependencies == 0u)
            enqueueLocked(handle.index);
        wakePlan = takeWorkerWakePlanLocked();
        wakeJoiners = m_joinWaiters != 0u;
        release.release();
    }
    notifyProgress(wakePlan, wakeJoiners);
    return handle;
}

CpuTaskScheduler::TaskNode* CpuTaskScheduler::resolveLocked(const TaskHandle handle)noexcept{
    if(!handle.valid() || handle.domainIdentity != m_domainIdentity || handle.index >= m_nodes.size())
        return nullptr;
    TaskNode& node = m_nodes[handle.index];
    return node.generation == handle.generation && node.state != TaskState::Free ? &node : nullptr;
}

const CpuTaskScheduler::TaskNode* CpuTaskScheduler::resolveLocked(const TaskHandle handle)const noexcept{
    if(!handle.valid() || handle.domainIdentity != m_domainIdentity || handle.index >= m_nodes.size())
        return nullptr;
    const TaskNode& node = m_nodes[handle.index];
    return node.generation == handle.generation && node.state != TaskState::Free ? &node : nullptr;
}

b8 CpuTaskScheduler::wasCanceledLocked(const TaskHandle handle)const noexcept{
    if(!handle.valid() || handle.domainIdentity != m_domainIdentity || handle.index >= m_nodes.size())
        return false;
    const TaskNode& node = m_nodes[handle.index];
    if(handle.generation == node.latestCanceledGeneration)
        return true;
    const auto found = LowerBound(node.olderCanceledGenerations.begin(), node.olderCanceledGenerations.end(), handle.generation);
    return found != node.olderCanceledGenerations.end() && *found == handle.generation;
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

void CpuTaskScheduler::enqueueLocked(const u32 index)noexcept{
    TaskNode& node = m_nodes[index];
    const usize queueIndex = QueueIndex(node.options);
    ReadyQueue& queue = m_ready[queueIndex];
    m_nonEmptyReadyMask |= static_cast<u16>(1u << queueIndex);
    node.state = TaskState::Ready;
    if(m_profileEnabled.load(MemoryOrder::relaxed))
        profileReadyLocked(index);
    if(node.options.target == CpuTaskTarget::Worker)
        ++m_readyWorkerCosts[node.options.cost];
    node.next = TaskHandle::s_InvalidIndex;
    if(queue.tail != TaskHandle::s_InvalidIndex)
        m_nodes[queue.tail].next = index;
    else
        queue.head = index;
    queue.tail = index;
}

u16 CpuTaskScheduler::eligibleQueueMaskLocked(const CpuAffinity::Enum affinity, const b8 mainThread, const b8 cooperative)const noexcept{
    u16 costMask = mainThread ? static_cast<u16>(1u << s_MainThreadTargetSlot) : 0u;
    if(!(mainThread && m_workerCount != 0u && !cooperative) && (mainThread || isExecuting())){
        costMask |= static_cast<u16>(1u << CpuTaskCost::Any);
        if(cooperative || affinity == CpuAffinity::Any || affinity == CpuAffinity::Performance || m_busyPerformance >= m_statistics.performanceWorkers)
            costMask |= static_cast<u16>(1u << CpuTaskCost::Heavy);
        if(cooperative || affinity == CpuAffinity::Any || affinity == CpuAffinity::Efficiency || m_busyEfficiency >= m_statistics.efficiencyWorkers)
            costMask |= static_cast<u16>(1u << CpuTaskCost::Light);
    }
    return static_cast<u16>(costMask | (costMask << s_CostSlotCount) | (costMask << (2u * s_CostSlotCount)));
}

CpuTaskScheduler::TaskHandle CpuTaskScheduler::claimLocked(const CpuAffinity::Enum affinity, const b8 mainThread, const b8 cooperative, ScopeWait* const preferredScope)noexcept{
    const u16 candidateMask = static_cast<u16>(m_nonEmptyReadyMask & eligibleQueueMaskLocked(affinity, mainThread, cooperative));
    if(candidateMask == 0u)
        return {};
    const u64 dispatch = m_dispatchCount;
    const usize firstPriority = dispatch % __hidden_cpu_task_scheduler::s_BackgroundAdmissionPeriod == __hidden_cpu_task_scheduler::s_BackgroundAdmissionTick
        ? static_cast<usize>(CpuTaskPriority::Background)
        : (dispatch % __hidden_cpu_task_scheduler::s_NormalAdmissionPeriod == __hidden_cpu_task_scheduler::s_NormalAdmissionTick ? static_cast<usize>(CpuTaskPriority::Normal) : static_cast<usize>(CpuTaskPriority::Critical))
    ;
    const usize preferredCost = affinity == CpuAffinity::Efficiency ? CpuTaskCost::Light : CpuTaskCost::Heavy;
    const Array<usize, s_CostSlotCount> costs = { s_MainThreadTargetSlot, preferredCost, 0u, preferredCost == CpuTaskCost::Heavy ? static_cast<usize>(CpuTaskCost::Light) : static_cast<usize>(CpuTaskCost::Heavy) };
    for(usize pass = 0u; pass < s_PrioritySlotCount; ++pass){
        const usize priority = pass == 0u ? firstPriority : (pass <= firstPriority ? pass - 1u : pass);
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
            const u32 nodeIndex = preferredScope ? findScopeReadyLocked(index, *preferredScope, previous) : queue.head;
            if(nodeIndex == TaskHandle::s_InvalidIndex)
                continue;
            TaskNode& node = m_nodes[nodeIndex];
            if(previous == TaskHandle::s_InvalidIndex)
                queue.head = node.next;
            else
                m_nodes[previous].next = node.next;
            if(queue.tail == nodeIndex)
                queue.tail = previous;
            if(queue.head == TaskHandle::s_InvalidIndex)
                m_nonEmptyReadyMask &= static_cast<u16>(~(1u << index));
            node.next = TaskHandle::s_InvalidIndex;
            node.state = TaskState::Running;
            if(node.options.target == CpuTaskTarget::Worker)
                --m_readyWorkerCosts[node.options.cost];
            ++m_dispatchCount;
            return { m_domainIdentity, nodeIndex, node.generation };
        }
    }
    return {};
}

b8 CpuTaskScheduler::hasReadyLocked(const CpuAffinity::Enum affinity, const b8 mainThread, const b8 cooperative, ScopeWait* const preferredScope)noexcept{
    const u16 candidateMask = static_cast<u16>(m_nonEmptyReadyMask & eligibleQueueMaskLocked(affinity, mainThread, cooperative));
    if(candidateMask == 0u)
        return false;
    if(!preferredScope)
        return true;
    for(u32 bits = candidateMask; bits != 0u; bits &= bits - 1u){
        const usize index = static_cast<usize>(CountTrailingZeros(bits));
        u32 previous = TaskHandle::s_InvalidIndex;
        if(findScopeReadyLocked(index, *preferredScope, previous) != TaskHandle::s_InvalidIndex)
            return true;
    }
    return false;
}

b8 CpuTaskScheduler::queueEligible(const usize queue, const CpuAffinity::Enum affinity, const b8 mainThread, const b8 cooperative)const noexcept{
    const usize cost = queue % s_CostSlotCount;
    if(cost == s_MainThreadTargetSlot)
        return mainThread;
    if(mainThread && m_workerCount != 0u && !cooperative)
        return false;
    if(!mainThread && !isExecuting())
        return false;
    if(cooperative || affinity == CpuAffinity::Any || cost == CpuTaskCost::Any)
        return true;
    if(cost == CpuTaskCost::Heavy)
        return affinity == CpuAffinity::Performance || m_busyPerformance >= m_statistics.performanceWorkers;
    return affinity == CpuAffinity::Efficiency || m_busyEfficiency >= m_statistics.efficiencyWorkers;
}

CpuTaskScheduler::ClaimedExecution CpuTaskScheduler::beginClaimedLocked(const TaskHandle handle, const usize workerIndex, const CpuAffinity::Enum affinity, const b8 cooperative, const b8 computeWakePlan)noexcept{
    ClaimedExecution claimed;
    claimed.handle = handle;
    claimed.node = resolveLocked(handle);
    if(!claimed.node)
        return claimed;
    TaskNode& node = *claimed.node;
    node.canceled = node.canceled || m_aborting || isScopeCanceledLocked(node.scope);
    TaskHandle ancestor = node.parent;
    while(TaskNode* parent = resolveLocked(ancestor)){
        node.canceled = node.canceled || parent->canceled || isScopeCanceledLocked(parent->scope);
        ancestor = parent->parent;
    }
    b8 reachedAffinitySaturation = false;
    if(workerIndex != 0u && m_workerDepth[workerIndex - 1u]++ == 0u){
        if(affinity == CpuAffinity::Performance){
            ++m_busyPerformance;
            reachedAffinitySaturation = m_busyPerformance == m_statistics.performanceWorkers;
        }
        else if(affinity == CpuAffinity::Efficiency){
            ++m_busyEfficiency;
            reachedAffinitySaturation = m_busyEfficiency == m_statistics.efficiencyWorkers;
        }
    }
    claimed.invoke = !node.canceled;
    if(cooperative)
        ++m_statistics.cooperativeTasks;
    if(claimed.invoke){
        switch(affinity){
        case CpuAffinity::Performance: ++m_statistics.performanceTasks; break;
        case CpuAffinity::Efficiency: ++m_statistics.efficiencyTasks; break;
        default: ++m_statistics.unclassifiedTasks; break;
        }
    }
    if(claimed.invoke && m_profileEnabled.load(MemoryOrder::relaxed)){
        claimed.profile = prepareProfileLocked(CpuTaskProfileKind::Execution, handle, {}, workerIndex, affinity);
        const ReadyProfile& ready = m_readyProfiles[handle.index];
        if(ready.captureEpoch == m_profileEpoch)
            claimed.readyProfile = ready;
    }
    if(computeWakePlan && reachedAffinitySaturation)
        claimed.wakePlan = takeWorkerWakePlanLocked();
    return claimed;
}

void CpuTaskScheduler::executeClaimed(ClaimedExecution& claimed, const usize workerIndex, const CpuAffinity::Enum affinity, ClaimedExecution* const nextClaim, const StopToken* const stop){
    if(!claimed.node)
        return;
    TaskNode& node = *claimed.node;
    const TaskHandle handle = claimed.handle;
    notifyWorkers(claimed.wakePlan);
    b8 succeeded = false;
    b8 spawnedChildren = false;
    ScopeExit finish([&]()noexcept{
        const Timer end = claimed.profile ? TimerNow() : Timer{};
        TaskHandle parentHandle;
        TaskNode* parentToRetire = nullptr;
        WorkerWakePlan wakePlan;
        b8 wakeJoiners = false;
        b8 readyToRetire = false;
        if(!spawnedChildren)
            node.function.reset();
        {
            ScopedLock lock(m_mutex);
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
            if(workerIndex != 0u && --m_workerDepth[workerIndex - 1u] == 0u){
                if(affinity == CpuAffinity::Performance)
                    --m_busyPerformance;
                else if(affinity == CpuAffinity::Efficiency)
                    --m_busyEfficiency;
            }
            if(!succeeded){
                m_aborting = true;
                m_abortingAtomic.store(true, MemoryOrder::release);
                node.canceled = true;
            }
            FOUNDATION_ASSERT(node.state == TaskState::Running);
            if(!spawnedChildren){
                FOUNDATION_ASSERT(node.children == 0u);
                node.state = TaskState::Retiring;
                b8 madeReady = false;
                b8 scopeCompleted = false;
                retireNodeLocked(handle, node, parentHandle, parentToRetire, madeReady, scopeCompleted);
                if(nextClaim && succeeded && !parentToRetire && stop && !stop->stop_requested()){
                    const TaskHandle nextHandle = claimLocked(affinity, false, false);
                    if(nextHandle.valid())
                        *nextClaim = beginClaimedLocked(nextHandle, workerIndex, affinity, false, false);
                }
                wakePlan = takeWorkerWakePlanLocked();
                wakeJoiners = m_joinWaiters != 0u && (madeReady || scopeCompleted || m_outstanding == 0u || m_handleJoinWaiters != 0u);
            }
            else{
                readyToRetire = node.children == 0u;
                node.state = readyToRetire ? TaskState::Retiring : TaskState::Children;
            }
        }
        if(!spawnedChildren){
            notifyProgress(wakePlan, wakeJoiners);
            if(parentToRetire)
                retire(parentHandle, *parentToRetire);
        }
        else if(readyToRetire)
            retire(handle, node);
    });
    {
        Execution execution(*this, handle, workerIndex, affinity, &spawnedChildren);
        if(claimed.profile)
            claimed.profile->begin = TimerNow();
        if(claimed.invoke)
            node.function();
    }
    succeeded = true;
}

void CpuTaskScheduler::retireNodeLocked(const TaskHandle handle, TaskNode& node, TaskHandle& outParentHandle, TaskNode*& outParentToRetire, b8& outMadeReady, b8& outScopeCompleted)noexcept{
    outParentHandle = {};
    outParentToRetire = nullptr;
    node.canceled = node.canceled || m_aborting || isScopeCanceledLocked(node.scope);
    outMadeReady = false;
    for(const TaskHandle dependentHandle : node.dependents){
        TaskNode* dependent = resolveLocked(dependentHandle);
        if(!dependent)
            continue;
        dependent->canceled = dependent->canceled || node.canceled;
        FOUNDATION_ASSERT(dependent->dependencies > 0u);
        if(--dependent->dependencies == 0u){
            enqueueLocked(dependentHandle.index);
            outMadeReady = true;
        }
    }
    if(TaskNode* parent = resolveLocked(node.parent)){
        parent->canceled = parent->canceled || node.canceled;
        FOUNDATION_ASSERT(parent->children > 0u);
        if(--parent->children == 0u && parent->state == TaskState::Children){
            parent->state = TaskState::Retiring;
            outParentHandle = node.parent;
            outParentToRetire = parent;
        }
    }
    outScopeCompleted = node.scope && node.scope->m_pending.fetch_sub(1u, MemoryOrder::acq_rel) == 1u;
    if(node.canceled){
        if(node.latestCanceledGeneration != 0u)
            node.olderCanceledGenerations.push_back(node.latestCanceledGeneration);
        node.latestCanceledGeneration = handle.generation;
        ++m_statistics.canceledTasks;
    }
    else
        ++m_statistics.completedTasks;
    FOUNDATION_ASSERT(m_outstanding > 0u);
    if(--m_outstanding == 0u)
        m_scopeSearchActive = false;
    else if(m_scopeSearchActive)
        invalidateScopeSearchLocked(true);
    node.dependents.clear();
    node.scope = nullptr;
    node.parent = {};
    node.state = TaskState::Free;
    if(node.generation != Limit<u32>::s_Max){
        ++node.generation;
        node.next = m_freeNode;
        m_freeNode = handle.index;
    }
}

void CpuTaskScheduler::retire(TaskHandle handle, TaskNode& first)noexcept{
    TaskNode* node = &first;
    while(node){
        FOUNDATION_ASSERT(node->state == TaskState::Retiring);
        node->function.reset();
        TaskHandle parentHandle;
        TaskNode* parentToRetire = nullptr;
        WorkerWakePlan wakePlan;
        b8 wakeJoiners = false;
        b8 madeReady = false;
        b8 scopeCompleted = false;
        {
            ScopedLock lock(m_mutex);
            retireNodeLocked(handle, *node, parentHandle, parentToRetire, madeReady, scopeCompleted);
            wakePlan = takeWorkerWakePlanLocked();
            wakeJoiners = m_joinWaiters != 0u && (madeReady || scopeCompleted || m_outstanding == 0u || m_handleJoinWaiters != 0u);
        }
        notifyProgress(wakePlan, wakeJoiners);
        handle = parentHandle;
        node = parentToRetire;
    }
}

void CpuTaskScheduler::workerLoop(const StopToken& stop, const usize workerIndex){
    const CpuWorkerPlacement placement = m_placements[workerIndex - 1u];
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
    Execution worker(*this, {}, workerIndex, affinity);
    ClaimedExecution claimed;
    for(;;){
        if(!claimed.node){
            UniqueLock lock(m_mutex);
            if(stop.stop_requested())
                return;
            if(!hasReadyLocked(affinity, false, false)){
                ++m_sleepingWorkers[affinity];
                ScopeExit unpark([this, affinity]()noexcept{
                    --m_sleepingWorkers[affinity];
                    if(m_wakingWorkers[affinity] > 0u)
                        --m_wakingWorkers[affinity];
                });
                Optional<ProfileSample> idle;
                if(m_profileEnabled.load(MemoryOrder::relaxed)){
                    idle = prepareProfileLocked(CpuTaskProfileKind::WorkerIdle, {}, {}, workerIndex, affinity);
                    idle->begin = TimerNow();
                }
                b8 sleeping = false;
                const b8 ready = m_workerChanged[affinity].wait(lock, stop, [this, affinity, &sleeping]()noexcept{
                    if(hasReadyLocked(affinity, false, false))
                        return true;
                    if(!sleeping)
                        sleeping = true;
                    else if(m_wakingWorkers[affinity] > 0u)
                        --m_wakingWorkers[affinity];
                    return false;
                });
                if(idle)
                    finishProfileLocked(*idle, TimerNow());
                if(!ready)
                    return;
            }
            const TaskHandle handle = claimLocked(affinity, false, false);
            if(handle.valid())
                claimed = beginClaimedLocked(handle, workerIndex, affinity, false);
        }
        ClaimedExecution nextClaimed;
        executeClaimed(claimed, workerIndex, affinity, &nextClaimed, &stop);
        claimed = Move(nextClaimed);
    }
}

b8 CpuTaskScheduler::executeOne(const b8 cooperative, ScopeWait* const preferredScope){
    ClaimedExecution claimed;
    const usize workerIndex = currentWorkerIndex();
    const CpuAffinity::Enum affinity = currentWorkerAffinity();
    {
        ScopedLock lock(m_mutex);
        const TaskHandle handle = claimLocked(affinity, isMainThread(), cooperative, preferredScope);
        if(handle.valid())
            claimed = beginClaimedLocked(handle, workerIndex, affinity, cooperative);
    }
    if(!claimed.node)
        return false;
    executeClaimed(claimed, workerIndex, affinity);
    return true;
}

b8 CpuTaskScheduler::isMainThread()const noexcept{
    return QueryCurrentThreadId() == m_mainThread;
}

void CpuTaskScheduler::beginLockedSearch()noexcept{
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

