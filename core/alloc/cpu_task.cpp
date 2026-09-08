// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpu_task.h"
#include "arena_names.h"

#include <global/exception.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CpuTaskScheduler::TaskNode::TaskNode(GlobalArena& arena)
    : dependents(arena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CpuTaskScheduler::Execution::Execution(
    CpuTaskScheduler& owner,
    TaskHandle handle,
    usize index,
    CpuAffinity::Enum workerAffinity
)noexcept
    : scheduler(owner)
    , task(handle)
    , workerIndex(index)
    , affinity(workerAffinity)
    , previous(s_execution)
{
    s_execution = this;
}
CpuTaskScheduler::Execution::~Execution(){
    s_execution = previous;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 CpuTaskScheduler::allocateDomainIdentity()noexcept{
    static Atomic<u64> next{ 1u };
    const u64 identity = next.fetch_add(1u, MemoryOrder::relaxed);
    if(identity == 0u)
        TerminateInvariant();
    return identity;
}

CpuTaskSchedulerConfig CpuTaskScheduler::workerConfig(const u32 workerCount){
    CpuTaskSchedulerConfig config;
    config.workerCount = workerCount;
    return config;
}

usize CpuTaskScheduler::queueIndex(const CpuTaskOptions& options)noexcept{
    const usize target = options.target == CpuTaskTarget::MainThread ? 3u : static_cast<usize>(options.cost);
    return static_cast<usize>(options.priority) * 4u + target;
}


CpuTaskScheduler::CpuTaskScheduler(const u32 workerCount)
    : CpuTaskScheduler(workerConfig(workerCount))
{}
CpuTaskScheduler::CpuTaskScheduler(const CpuTaskSchedulerConfig& config)
    : m_domainIdentity(allocateDomainIdentity())
    , m_mainThread(QueryCurrentThreadId())
    , m_arena(ArenaScope::s_CpuTaskScheduler)
    , m_nodes(m_arena)
    , m_placements(m_arena)
    , m_workerDepth(m_arena)
    , m_searchStack(m_arena)
    , m_searchVisits(m_arena)
    , m_canceledHandles(m_arena)
    , m_workers(m_arena)
{
    InteropVector<CpuWorkerPlacement> topology;
    if(!QueryCpuWorkerPlacements(topology))
        topology.clear();
    Sort(topology.begin(), topology.end(), [](const CpuWorkerPlacement& lhs, const CpuWorkerPlacement& rhs){
        return lhs.performanceClass > rhs.performanceClass;
    });
    const u32 available = topology.empty() ? Max(QueryCpuCoreCount(CpuAffinity::Any), 1u) : static_cast<u32>(topology.size());
    const u32 reserved = Min(config.reservedThreadCount, available);
    m_workerCount = config.workerCount == CpuTaskSchedulerConfig::s_AutomaticWorkerCount ? available - reserved : config.workerCount;
    m_placements.reserve(m_workerCount);
    // Reserve fastest CPUs for the caller by default; explicit worker counts retain all available processor identities.
    const usize first = config.workerCount == CpuTaskSchedulerConfig::s_AutomaticWorkerCount ? reserved : 0u;
    for(u32 worker = 0u; worker < m_workerCount; ++worker){
        CpuWorkerPlacement placement;
        if(!topology.empty()){
            const usize usable = topology.size() - Min(first, topology.size() - 1u);
            usize selected = first + static_cast<usize>(worker) % usable;
            // A small explicit budget still includes both capacity classes when at least two workers are requested.
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


void CpuTaskScheduler::wait(const TaskHandle handle){
    if(!handle.valid())
        return;
    {
        ScopedLock lock(m_mutex);
        if(handle.domainIdentity != m_domainIdentity)
            throw RuntimeException("CPU task wait belongs to another scheduler");
        if(++m_searchGeneration == 0u){
            for(u64& visit : m_searchVisits)
                visit = 0u;
            ++m_searchGeneration;
        }
        m_searchStack.clear();
        const auto visit = [this](const TaskHandle candidate){
            if(resolveLocked(candidate) && m_searchVisits[candidate.index] != m_searchGeneration){
                m_searchVisits[candidate.index] = m_searchGeneration;
                m_searchStack.push_back(candidate.index);
            }
        };
        for(Execution* current = s_execution; current; current = current->previous){
            if(&current->scheduler == this)
                visit(current->task);
        }
        // Dependent tasks and structured parents cannot complete until the current execution finishes.
        for(usize cursor = 0u; cursor < m_searchStack.size(); ++cursor){
            const u32 index = m_searchStack[cursor];
            const TaskNode& node = m_nodes[index];
            if(index == handle.index && node.generation == handle.generation)
                throw RuntimeException("CPU task cannot wait for itself, an ancestor, or dependent work");
            visit(node.parent);
            for(const TaskHandle dependent : node.dependents)
                visit(dependent);
        }
    }
    while(!isComplete(handle)){
        if(executeOne(isExecuting()))
            continue;
        UniqueLock lock(m_mutex);
        m_changed.wait(lock, [this, handle](){
            return !resolveLocked(handle) || hasReadyLocked(currentWorkerAffinity(), isMainThread(), isExecuting());
        });
    }
}

void CpuTaskScheduler::wait(){
    if(isExecuting())
        throw RuntimeException("CPU task cannot wait for its entire scheduler");
    for(;;){
        {
            ScopedLock lock(m_mutex);
            if(m_outstanding == 0u)
                return;
        }
        if(executeOne(false))
            continue;
        UniqueLock lock(m_mutex);
        m_changed.wait(lock, [this](){
            return m_outstanding == 0u || hasReadyLocked(CpuAffinity::Any, isMainThread(), false);
        });
    }
}

void CpuTaskScheduler::drain()noexcept{
    {
        ScopedLock lock(m_mutex);
        m_aborting = true;
    }
    notifyProgress();
    wait();
}

void CpuTaskScheduler::pumpMainThread(){
    if(!isMainThread())
        throw RuntimeException("CPU main-thread tasks require the scheduler owner thread");
    while(executeOne(false)){}
}

bool CpuTaskScheduler::isComplete(const TaskHandle handle)const{
    if(!handle.valid())
        return true;
    ScopedLock lock(m_mutex);
    return !resolveLocked(handle);
}

CpuTaskSchedulerStatistics CpuTaskScheduler::statistics()const{
    ScopedLock lock(m_mutex);
    CpuTaskSchedulerStatistics result = m_statistics;
    result.outstandingTasks = m_outstanding;
    return result;
}


CpuTaskScheduler::TaskHandle CpuTaskScheduler::submitTask(
    TaskFunction&& function,
    CpuTaskScope* const scope,
    const CpuTaskOptions options,
    const TaskHandle* const dependencies,
    const usize dependencyCount){
    if(
        !function || (dependencyCount != 0u && !dependencies)
        || options.cost > CpuTaskCost::Light || options.priority > CpuTaskPriority::Background
        || options.target > CpuTaskTarget::MainThread
    )
        return {};

    TaskHandle handle;
    TaskNode* node;
    u32 wakeMask;
    {
        ScopedLock lock(m_mutex);
        if(m_aborting || (scope && scope->m_canceled))
            return {};
        for(usize index = 0u; index < dependencyCount; ++index){
            if(dependencies[index].valid() && dependencies[index].domainIdentity != m_domainIdentity)
                return {};
        }
        u32 index = m_freeNode;
        if(index == TaskHandle::s_InvalidIndex){
            if(m_nodes.size() >= TaskHandle::s_InvalidIndex)
                return {};
            index = static_cast<u32>(m_nodes.size());
            ContainerDetail::ReserveGrowingCapacity(m_searchStack, m_nodes.size() + 1u);
            m_searchVisits.resize(m_nodes.size() + 1u, 0u);
            m_nodes.emplace_back(m_arena);
        }
        else
            m_freeNode = m_nodes[index].next;
        node = &m_nodes[index];
        node->state = TaskState::Preparing;
        node->next = TaskHandle::s_InvalidIndex;
        handle = { m_domainIdentity, index, node->generation };
    }
    ScopeExit release([&]()noexcept{ releaseReservation(handle); });

    node->function = Move(function);
    {
        ScopedLock lock(m_mutex);
        if(m_aborting || (scope && scope->m_canceled))
            return {};
        ContainerDetail::ReserveGrowingCapacity(m_canceledHandles, AddSize(AddSize(m_canceledHandles.size(), m_outstanding), 1u));
        if(dependencyCount != 0u && s_execution && &s_execution->scheduler == this && s_execution->task.valid()){
            ScratchArena scratch(ArenaScope::s_CpuTaskDependencies, 4096u);
            Vector<TaskHandle, ScratchArena> frontier(scratch);
            Vector<u8, ScratchArena> visited(m_nodes.size(), 0u, scratch);
            frontier.push_back(s_execution->task);
            for(usize cursor = 0u; cursor < frontier.size(); ++cursor){
                const TaskHandle candidate = frontier[cursor];
                TaskNode* reachable = resolveLocked(candidate);
                if(!reachable || visited[candidate.index] != 0u)
                    continue;
                visited[candidate.index] = 1u;
                for(usize dependency = 0u; dependency < dependencyCount; ++dependency){
                    if(
                        dependencies[dependency].domainIdentity == m_domainIdentity
                        && dependencies[dependency].index == candidate.index
                        && dependencies[dependency].generation == candidate.generation
                    )
                        return {};
                }
                for(const TaskHandle dependent : reachable->dependents)
                    frontier.push_back(dependent);
                if(reachable->parent.valid())
                    frontier.push_back(reachable->parent);
            }
        }
        // Reserve all dependency storage before publishing any edge. Duplicate predecessors are valid fan-in edges.
        m_searchStack.clear();
        m_searchStack.reserve(dependencyCount);
        for(usize index = 0u; index < dependencyCount; ++index){
            if(resolveLocked(dependencies[index]))
                m_searchStack.push_back(dependencies[index].index);
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
        node->scope = scope;
        node->options = options;
        node->parent = {};
        node->dependencies = 0u;
        node->children = 0u;
        node->canceled = false;
        if(s_execution && &s_execution->scheduler == this){
            if(TaskNode* parent = resolveLocked(s_execution->task)){
                node->parent = s_execution->task;
                ++parent->children;
                node->canceled = parent->canceled;
            }
        }
        for(usize index = 0u; index < dependencyCount; ++index){
            if(TaskNode* dependency = resolveLocked(dependencies[index])){
                dependency->dependents.push_back(handle);
                ++node->dependencies;
                node->canceled = node->canceled || dependency->canceled;
            }
            else{
                for(const TaskHandle canceled : m_canceledHandles){
                    if(
                        canceled.domainIdentity == dependencies[index].domainIdentity
                        && canceled.index == dependencies[index].index && canceled.generation == dependencies[index].generation
                    ){
                        node->canceled = true;
                        break;
                    }
                }
            }
        }
        ++m_outstanding;
        m_statistics.peakOutstandingTasks = Max(m_statistics.peakOutstandingTasks, m_outstanding);
        if(scope)
            scope->m_pending.fetch_add(1u, MemoryOrder::release);
        node->state = TaskState::Waiting;
        if(node->dependencies == 0u)
            enqueueLocked(handle.index);
        wakeMask = workerWakeMaskLocked();
    }
    release.release();
    notifyProgress(wakeMask);
    return handle;
}

void CpuTaskScheduler::releaseReservation(const TaskHandle handle)noexcept{
    TaskNode* node;
    {
        ScopedLock lock(m_mutex);
        node = resolveLocked(handle);
    }
    NWB_ASSERT(node);
    node->function.reset();
    ScopedLock lock(m_mutex);
    node->state = TaskState::Free;
    node->next = m_freeNode;
    m_freeNode = handle.index;
}

CpuTaskScheduler::TaskNode* CpuTaskScheduler::resolveLocked(const TaskHandle handle)const noexcept{
    if(!handle.valid() || handle.domainIdentity != m_domainIdentity || handle.index >= m_nodes.size())
        return nullptr;
    TaskNode& node = const_cast<TaskNode&>(m_nodes[handle.index]);
    return node.generation == handle.generation && node.state != TaskState::Free ? &node : nullptr;
}

void CpuTaskScheduler::enqueueLocked(const u32 index)noexcept{
    TaskNode& node = m_nodes[index];
    ReadyQueue& queue = m_ready[queueIndex(node.options)];
    node.state = TaskState::Ready;
    if(node.options.target == CpuTaskTarget::Worker)
        ++m_readyWorkerCosts[node.options.cost];
    node.next = TaskHandle::s_InvalidIndex;
    if(queue.tail != TaskHandle::s_InvalidIndex)
        m_nodes[queue.tail].next = index;
    else
        queue.head = index;
    queue.tail = index;
}

CpuTaskScheduler::TaskHandle CpuTaskScheduler::claimLocked(
    const CpuAffinity::Enum affinity,
    const bool mainThread,
    const bool cooperative,
    CpuTaskScope* const preferredScope)noexcept{
    // Periodically admit background and normal work even while critical producers keep publishing.
    const u64 dispatch = m_dispatchCount;
    const usize firstPriority = dispatch % 32u == 31u ? 2u : (dispatch % 8u == 7u ? 1u : 0u);
    const usize preferredCost = affinity == CpuAffinity::Efficiency ? CpuTaskCost::Light : CpuTaskCost::Heavy;
    const usize costs[4] = { 3u, preferredCost, 0u, preferredCost == CpuTaskCost::Heavy ? 2u : 1u };
    for(usize pass = 0u; pass < 3u; ++pass){
        const usize priority = pass == 0u ? firstPriority : (pass <= firstPriority ? pass - 1u : pass);
        for(const usize cost : costs){
            const usize index = priority * 4u + cost;
            ReadyQueue& queue = m_ready[index];
            if(queue.head == TaskHandle::s_InvalidIndex || !queueEligible(index, affinity, mainThread, cooperative))
                continue;
            u32 previous = TaskHandle::s_InvalidIndex;
            u32 nodeIndex = queue.head;
            while(
                preferredScope && nodeIndex != TaskHandle::s_InvalidIndex
                && !contributesToScopeLocked(nodeIndex, *preferredScope)
            ){
                previous = nodeIndex;
                nodeIndex = m_nodes[nodeIndex].next;
            }
            if(nodeIndex == TaskHandle::s_InvalidIndex)
                continue;
            TaskNode& node = m_nodes[nodeIndex];
            if(previous == TaskHandle::s_InvalidIndex)
                queue.head = node.next;
            else
                m_nodes[previous].next = node.next;
            if(queue.tail == nodeIndex)
                queue.tail = previous;
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

bool CpuTaskScheduler::hasReadyLocked(
    const CpuAffinity::Enum affinity,
    const bool mainThread,
    const bool cooperative,
    CpuTaskScope* const preferredScope)noexcept{
    for(usize index = 0u; index < s_QueueCount; ++index){
        if(!queueEligible(index, affinity, mainThread, cooperative))
            continue;
        for(u32 node = m_ready[index].head; node != TaskHandle::s_InvalidIndex; node = m_nodes[node].next){
            if(!preferredScope || contributesToScopeLocked(node, *preferredScope))
                return true;
        }
    }
    return false;
}

bool CpuTaskScheduler::contributesToScopeLocked(const u32 index, const CpuTaskScope& scope)noexcept{
    if(++m_searchGeneration == 0u){
        for(u64& visit : m_searchVisits)
            visit = 0u;
        ++m_searchGeneration;
    }
    m_searchStack.clear();
    m_searchStack.push_back(index);
    m_searchVisits[index] = m_searchGeneration;
    for(usize cursor = 0u; cursor < m_searchStack.size(); ++cursor){
        const TaskNode& node = m_nodes[m_searchStack[cursor]];
        if(node.scope == &scope)
            return true;
        const auto visit = [this](const TaskHandle handle){
            if(resolveLocked(handle) && m_searchVisits[handle.index] != m_searchGeneration){
                m_searchVisits[handle.index] = m_searchGeneration;
                m_searchStack.push_back(handle.index);
            }
        };
        visit(node.parent);
        for(const TaskHandle dependent : node.dependents)
            visit(dependent);
    }
    return false;
}

bool CpuTaskScheduler::queueEligible(
    const usize queue,
    const CpuAffinity::Enum affinity,
    const bool mainThread,
    const bool cooperative)const noexcept{
    const usize cost = queue % 4u;
    if(cost == 3u)
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

void CpuTaskScheduler::execute(
    const TaskHandle handle,
    const usize workerIndex,
    const CpuAffinity::Enum affinity,
    const bool cooperative){
    TaskNode* node;
    bool invoke;
    u32 wakeMask;
    {
        ScopedLock lock(m_mutex);
        node = resolveLocked(handle);
        if(!node)
            return;
        node->canceled = node->canceled || m_aborting || (node->scope && node->scope->m_canceled);
        TaskHandle ancestor = node->parent;
        while(TaskNode* parent = resolveLocked(ancestor)){
            node->canceled = node->canceled || parent->canceled || (parent->scope && parent->scope->m_canceled);
            ancestor = parent->parent;
        }
        if(workerIndex != 0u && m_workerDepth[workerIndex - 1u]++ == 0u){
            if(affinity == CpuAffinity::Performance)
                ++m_busyPerformance;
            else if(affinity == CpuAffinity::Efficiency)
                ++m_busyEfficiency;
        }
        invoke = !node->canceled;
        if(cooperative)
            ++m_statistics.cooperativeTasks;
        if(invoke){
            switch(affinity){
            case CpuAffinity::Performance: ++m_statistics.performanceTasks; break;
            case CpuAffinity::Efficiency: ++m_statistics.efficiencyTasks; break;
            default: ++m_statistics.unclassifiedTasks; break;
            }
        }
        wakeMask = workerWakeMaskLocked();
    }
    notifyWorkers(wakeMask);
    bool succeeded = false;
    ScopeExit finish([&]()noexcept{
        {
            ScopedLock lock(m_mutex);
            if(workerIndex != 0u && --m_workerDepth[workerIndex - 1u] == 0u){
                if(affinity == CpuAffinity::Performance)
                    --m_busyPerformance;
                else if(affinity == CpuAffinity::Efficiency)
                    --m_busyEfficiency;
            }
        }
        finishBody(handle, succeeded);
    });
    {
        Execution execution(*this, handle, workerIndex, affinity);
        if(invoke)
            node->function();
    }
    succeeded = true;
}

void CpuTaskScheduler::finishBody(const TaskHandle handle, const bool succeeded)noexcept{
    bool readyToRetire;
    {
        ScopedLock lock(m_mutex);
        TaskNode* node = resolveLocked(handle);
        if(!node)
            return;
        if(!succeeded){
            m_aborting = true;
            node->canceled = true;
        }
        node->state = TaskState::Children;
        readyToRetire = node->children == 0u;
        if(readyToRetire)
            node->state = TaskState::Retiring;
    }
    if(readyToRetire)
        retire(handle);
}

void CpuTaskScheduler::retire(TaskHandle handle)noexcept{
    while(handle.valid()){
        TaskNode* node;
        {
            ScopedLock lock(m_mutex);
            node = resolveLocked(handle);
            if(!node || node->state != TaskState::Retiring)
                return;
        }
        // A task retains its callable until every descendant completes. Capture destruction precedes dependent publication.
        node->function.reset();
        TaskHandle parentToRetire;
        u32 wakeMask;
        {
            ScopedLock lock(m_mutex);
            node->canceled = node->canceled || m_aborting || (node->scope && node->scope->m_canceled);
            for(const TaskHandle dependentHandle : node->dependents){
                TaskNode* dependent = resolveLocked(dependentHandle);
                if(!dependent)
                    continue;
                dependent->canceled = dependent->canceled || node->canceled;
                NWB_ASSERT(dependent->dependencies > 0u);
                if(--dependent->dependencies == 0u)
                    enqueueLocked(dependentHandle.index);
            }
            if(TaskNode* parent = resolveLocked(node->parent)){
                parent->canceled = parent->canceled || node->canceled;
                NWB_ASSERT(parent->children > 0u);
                if(--parent->children == 0u && parent->state == TaskState::Children){
                    parent->state = TaskState::Retiring;
                    parentToRetire = node->parent;
                }
            }
            if(node->scope)
                node->scope->m_pending.fetch_sub(1u, MemoryOrder::acq_rel);
            if(node->canceled){
                m_canceledHandles.push_back(handle);
                ++m_statistics.canceledTasks;
            }
            else
                ++m_statistics.completedTasks;
            NWB_ASSERT(m_outstanding > 0u);
            --m_outstanding;
            node->dependents.clear();
            node->scope = nullptr;
            node->parent = {};
            node->state = TaskState::Free;
            if(node->generation != Limit<u32>::s_Max){
                ++node->generation;
                node->next = m_freeNode;
                m_freeNode = handle.index;
            }
            wakeMask = workerWakeMaskLocked();
        }
        notifyProgress(wakeMask);
        handle = parentToRetire;
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
    for(;;){
        TaskHandle handle;
        {
            UniqueLock lock(m_mutex);
            if(stop.stop_requested())
                return;
            if(!hasReadyLocked(affinity, false, false)){
                ++m_sleepingWorkers[affinity];
                ScopeExit unpark([this, affinity]()noexcept{ --m_sleepingWorkers[affinity]; });
                if(!m_workerChanged[affinity].wait(lock, stop, [this, affinity](){ return hasReadyLocked(affinity, false, false); }))
                    return;
            }
            handle = claimLocked(affinity, false, false);
        }
        if(handle.valid())
            execute(handle, workerIndex, affinity, false);
    }
}

bool CpuTaskScheduler::executeOne(const bool cooperative, CpuTaskScope* const preferredScope){
    TaskHandle handle;
    const usize workerIndex = currentWorkerIndex();
    const CpuAffinity::Enum affinity = currentWorkerAffinity();
    {
        ScopedLock lock(m_mutex);
        handle = claimLocked(affinity, isMainThread(), cooperative, preferredScope);
    }
    if(!handle.valid())
        return false;
    execute(handle, workerIndex, affinity, cooperative);
    return true;
}

void CpuTaskScheduler::drainTask(const TaskHandle handle)noexcept{
    {
        ScopedLock lock(m_mutex);
        m_aborting = true;
    }
    notifyProgress();
    wait(handle);
}

void CpuTaskScheduler::waitScope(CpuTaskScope& scope){
    {
        ScopedLock lock(m_mutex);
        for(Execution* current = s_execution; current; current = current->previous){
            if(&current->scheduler != this)
                continue;
            TaskHandle ancestor = current->task;
            while(TaskNode* node = resolveLocked(ancestor)){
                if(node->scope == &scope)
                    throw RuntimeException("CPU task cannot join a scope that contains itself");
                ancestor = node->parent;
            }
        }
    }
    while(scope.m_pending.load(MemoryOrder::acquire) != 0u){
        if(executeOne(isExecuting() || scope.m_allowCallerWork, &scope))
            continue;
        UniqueLock lock(m_mutex);
        m_changed.wait(lock, [this, &scope](){
            return
                scope.m_pending.load(MemoryOrder::acquire) == 0u
                || hasReadyLocked(currentWorkerAffinity(), isMainThread(), isExecuting() || scope.m_allowCallerWork, &scope)
            ;
        });
    }
}

bool CpuTaskScheduler::isMainThread()const noexcept{
    return QueryCurrentThreadId() == m_mainThread;
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


void CpuTaskScope::wait(){
    m_scheduler.waitScope(*this);
}

void CpuTaskScope::drain()noexcept{
    {
        ScopedLock lock(m_scheduler.m_mutex);
        m_scheduler.m_aborting = true;
    }
    m_scheduler.notifyProgress();
    m_scheduler.waitScope(*this);
}

void CpuTaskScope::cancel()noexcept{
    {
        ScopedLock lock(m_scheduler.m_mutex);
        m_canceled = true;
    }
    m_scheduler.notifyProgress();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

