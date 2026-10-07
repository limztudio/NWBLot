// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiled_graph.h"

#include "task_graph.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_compiled_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] u64 AllocateObjectIdentity()noexcept{
    static Atomic<u64> s_NextObjectIdentity{ 1u };

    u64 identity = s_NextObjectIdentity.load(MemoryOrder::relaxed);
    while(identity != 0u && identity != Limit<u64>::s_Max){
        if(s_NextObjectIdentity.compare_exchange_weak(
            identity,
            identity + 1u,
            MemoryOrder::relaxed,
            MemoryOrder::relaxed
        ))
            return identity;
    }
    NWB_FATAL_ASSERT_MSG(false, "GpuCompiledGraph object identity space is exhausted");
    TerminateInvariant();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCompiledGraph::CompilationScope::CompilationScope(GpuCompiledGraph& graph)noexcept{
    {
        NothrowScopedLock lock(graph.m_attemptBindingMutex);
        if(
            graph.m_attemptBindingState == AttemptBindingState::Recording
            || graph.m_attemptBindingState == AttemptBindingState::Submitting
        )
            return;

        u32 expectedPlanAccessState = 0u;
        if(!graph.m_planAccessState.compare_exchange_strong(
            expectedPlanAccessState,
            GpuCompiledGraph::s_PlanAccessWriterBit,
            MemoryOrder::acq_rel,
            MemoryOrder::acquire
        ))
            return;
        graph.clearAttemptBindingWithinLock();
    }

    graph.resetPlanStorageWithinPlanWriteScope();
    m_graph = &graph;
}
GpuCompiledGraph::CompilationScope::~CompilationScope()noexcept{
    if(!m_graph)
        return;

    if(m_graph->m_planAccessState.load(MemoryOrder::relaxed) != GpuCompiledGraph::s_PlanAccessWriterBit){
        NWB_FATAL_ASSERT_MSG(false, "Compiled-plan construction must retain its exact writer claim");
        TerminateInvariant();
    }
    if(!m_published)
        m_graph->resetPlanStorageWithinPlanWriteScope();
    m_graph->m_planAccessState.store(0u, MemoryOrder::release);
}


void GpuCompiledGraph::CompilationScope::publish()noexcept{
    const bool publicationValid = m_graph
        && m_graph->m_planAccessState.load(MemoryOrder::relaxed) == GpuCompiledGraph::s_PlanAccessWriterBit
    ;
    NWB_FATAL_ASSERT_MSG(publicationValid, "Compiled-plan publication requires exact compile-writer ownership");
    if(!publicationValid)
        TerminateInvariant();

    m_graph->m_valid = true;
    m_published = true;
}




void GpuCompiledGraph::clearAttemptBindingWithinLock()noexcept{
    m_attemptGraph = nullptr;
    m_attemptPlanGeneration = 0u;
    m_attemptRecordingGeneration = 0u;
    m_attemptTransactionIdentity = 0u;
    m_attemptTransactionResetGeneration = 0u;
    m_attemptBindingState = AttemptBindingState::None;
}


void GpuCompiledGraph::resetPlanStorageWithinPlanWriteScope()noexcept{
    static_assert(noexcept(m_tasks.clear()));
    static_assert(noexcept(m_compiledTaskIndexByTask.clear()));
    static_assert(noexcept(m_packets.clear()));
    static_assert(noexcept(m_packetTasks.clear()));
    static_assert(noexcept(m_packetDependencies.clear()));
    static_assert(noexcept(m_packetExternalDependencies.clear()));
    static_assert(noexcept(m_prologueStateSeeds.clear()));
    static_assert(noexcept(m_prologueBarriers.clear()));
    static_assert(noexcept(m_epilogueBarriers.clear()));
    static_assert(noexcept(m_ownershipTransfers.clear()));
    static_assert(noexcept(m_externalResourceExports.clear()));
    static_assert(noexcept(m_externalResourceExportSources.clear()));
    static_assert(noexcept(m_queueTopology.clear()));
    static_assert(noexcept(m_physicalQueueCompileStatistics.clear()));

    m_tasks.clear();
    m_compiledTaskIndexByTask.clear();
    m_packets.clear();
    m_packetTasks.clear();
    m_packetDependencies.clear();
    m_packetExternalDependencies.clear();
    m_prologueStateSeeds.clear();
    m_prologueBarriers.clear();
    m_epilogueBarriers.clear();
    m_ownershipTransfers.clear();
    m_externalResourceExports.clear();
    m_externalResourceExportSources.clear();
    m_queueTopology.clear();
    m_physicalQueueCompileStatistics.clear();
    m_presentEndpoint = {};
    m_packetTimingEnvelopeRange = {};
    m_generation = 0u;
    m_declarationRevision = 0u;
    m_planGeneration = 0u;
    m_deviceGeneration = 0u;
    m_graphTaskCount = 0u;
    m_compileStatistics = {};
    m_hasPresentEndpoint = false;
    m_valid = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCompiledGraph::GpuCompiledGraph(GraphicsArena& arena)
    : m_tasks(arena)
    , m_compiledTaskIndexByTask(arena)
    , m_packets(arena)
    , m_packetTasks(arena)
    , m_packetDependencies(arena)
    , m_packetExternalDependencies(arena)
    , m_prologueStateSeeds(arena)
    , m_prologueBarriers(arena)
    , m_epilogueBarriers(arena)
    , m_ownershipTransfers(arena)
    , m_externalResourceExports(arena)
    , m_externalResourceExportSources(arena)
    , m_queueTopology(arena)
    , m_physicalQueueCompileStatistics(arena)
    , m_objectIdentity(__hidden_gpu_compiled_graph::AllocateObjectIdentity())
{}


GpuCompiledGraph::~GpuCompiledGraph(){
    u32 expectedPlanAccessState = 0u;
    if(!m_planAccessState.compare_exchange_strong(
        expectedPlanAccessState,
        s_PlanAccessWriterBit,
        MemoryOrder::acq_rel,
        MemoryOrder::acquire
    )){
        NWB_FATAL_ASSERT_MSG(false, "GpuCompiledGraph destruction requires all plan readers to resolve first");
        TerminateInvariant();
    }

    NothrowScopedLock lock(m_attemptBindingMutex);
    if(
        m_attemptBindingState == AttemptBindingState::Recording
        || m_attemptBindingState == AttemptBindingState::Submitting
    ){
        NWB_FATAL_ASSERT_MSG(false, "GpuCompiledGraph destruction requires plan readers and its active graph attempt to resolve first");
        TerminateInvariant();
    }
}


bool GpuCompiledGraph::tryReset(){
    {
        NothrowScopedLock lock(m_attemptBindingMutex);
        if(
            m_attemptBindingState == AttemptBindingState::Recording
            || m_attemptBindingState == AttemptBindingState::Submitting
        )
            return false;

        u32 expectedPlanAccessState = 0u;
        if(!m_planAccessState.compare_exchange_strong(
            expectedPlanAccessState,
            s_PlanAccessWriterBit,
            MemoryOrder::acq_rel,
            MemoryOrder::acquire
        ))
            return false;
        clearAttemptBindingWithinLock();
    }
    resetPlanStorageWithinPlanWriteScope();
    m_planAccessState.store(0u, MemoryOrder::release);
    return true;
}


void GpuCompiledGraph::reset(){
    if(tryReset())
        return;

    NWB_FATAL_ASSERT_MSG(false, "GpuCompiledGraph::reset requires its active submission owner to resolve first");
    TerminateInvariant();
}

bool GpuCompiledGraph::validPacket(const GpuSubmissionPacketId& packetID)const noexcept{
    return valid() && packetID.valid() && packetID.generation == m_planGeneration && packetID.index < m_packets.size();
}

bool GpuCompiledGraph::validPacketRange(const GpuSubmissionPacketRange& range)const noexcept{
    return range.valid()
        && validPacket(range.first)
        && range.packetCount <= m_packets.size() - range.first.index
    ;
}

GpuSubmissionPacketId GpuCompiledGraph::packetIdAt(const usize index)const noexcept{
    return index < m_packets.size()
        ? GpuSubmissionPacketId{ .generation = m_planGeneration, .index = static_cast<u32>(index) }
        : GpuSubmissionPacketId{}
    ;
}

GpuSubmissionPacketRange GpuCompiledGraph::packetRange(
    const GpuSubmissionPacketId& first,
    const GpuSubmissionPacketId& last
)const noexcept{
    if(!validPacket(first) || !validPacket(last) || last.index < first.index)
        return {};
    return GpuSubmissionPacketRange{
        .first = first,
        .packetCount = static_cast<usize>(last.index) - static_cast<usize>(first.index) + 1u,
    };
}

GpuSubmissionPacketRange GpuCompiledGraph::packetRangeForTasks(
    const GpuTaskId& first,
    const GpuTaskId& last
)const noexcept{
    const GpuSubmissionPacketId firstPacket = packetForTask(first);
    const GpuSubmissionPacketId lastPacket = packetForTask(last);
    return packetRange(firstPacket, lastPacket);
}

GpuSubmissionPacketRange GpuCompiledGraph::allPacketRange()const noexcept{
    return m_packets.empty()
        ? GpuSubmissionPacketRange{}
        : packetRange(packetIdAt(0u), packetIdAt(m_packets.size() - 1u))
    ;
}

const GpuCompiledTask* GpuCompiledGraph::findTask(const GpuTaskId& task)const noexcept{
    if(
        !task.valid()
        || task.generation != m_generation
        || task.index >= m_compiledTaskIndexByTask.size()
    )
        return nullptr;
    const u32 compiledTaskIndex = m_compiledTaskIndexByTask[task.index];
    if(compiledTaskIndex >= m_tasks.size())
        return nullptr;
    const GpuCompiledTask& compiledTask = m_tasks[compiledTaskIndex];
    return compiledTask.task == task ? &compiledTask : nullptr;
}

GpuSubmissionPacketId GpuCompiledGraph::packetForTask(const GpuTaskId& task)const noexcept{
    const GpuCompiledTask* const compiledTask = findTask(task);
    return compiledTask ? compiledTask->packet : GpuSubmissionPacketId{};
}

GpuTaskPacketizationDecision::Enum GpuCompiledGraph::packetizationDecisionForTask(const GpuTaskId& task)const noexcept{
    const GpuCompiledTask* const compiledTask = findTask(task);
    return compiledTask ? compiledTask->packetizationDecision : GpuTaskPacketizationDecision::Unknown;
}

bool GpuCompiledGraph::tasksSharePacket(
    const GpuTaskId& first,
    const GpuTaskId& second
)const noexcept{
    const GpuCompiledTask* const firstTask = findTask(first);
    const GpuCompiledTask* const secondTask = findTask(second);
    return firstTask && secondTask && firstTask->packet == secondTask->packet;
}

bool GpuCompiledGraph::taskPrecedesOrSharesPacket(
    const GpuTaskId& first,
    const GpuTaskId& second
)const noexcept{
    const GpuCompiledTask* const firstTask = findTask(first);
    const GpuCompiledTask* const secondTask = findTask(second);
    return firstTask
        && secondTask
        && firstTask->packet.valid()
        && secondTask->packet.valid()
        && firstTask->packet.index <= secondTask->packet.index
    ;
}

bool GpuCompiledGraph::taskPrecedesInSamePacket(
    const GpuTaskId& first,
    const GpuTaskId& second
)const noexcept{
    if(first == second)
        return false;
    const GpuCompiledTask* const firstTask = findTask(first);
    const GpuCompiledTask* const secondTask = findTask(second);
    if(
        !firstTask
        || !secondTask
        || firstTask->packet != secondTask->packet
        || !validPacket(firstTask->packet)
    )
        return false;

    const GpuSubmissionPacket& packetPlan = m_packets[firstTask->packet.index];
    if(
        packetPlan.taskCount == 0u
        || packetPlan.taskOffset > m_packetTasks.size()
        || packetPlan.taskCount > m_packetTasks.size() - packetPlan.taskOffset
    )
        return false;

    // Packetization appends compiled tasks and packet task IDs together in execution order.
    const u32 firstTaskIndex = m_compiledTaskIndexByTask[first.index];
    const u32 secondTaskIndex = m_compiledTaskIndexByTask[second.index];
    NWB_ASSERT(m_packetTasks[firstTaskIndex] == first);
    NWB_ASSERT(m_packetTasks[secondTaskIndex] == second);
    return firstTaskIndex < secondTaskIndex;
}

bool GpuCompiledGraph::tasksFormContiguousPacketSequence(
    const GpuTaskId* const tasks,
    const usize taskCount
)const noexcept{
    if(!tasks || taskCount == 0u)
        return false;
    const GpuCompiledTask* const firstTask = findTask(tasks[0u]);
    if(!firstTask || !validPacket(firstTask->packet))
        return false;

    const GpuSubmissionPacket& packetPlan = m_packets[firstTask->packet.index];
    if(
        taskCount > packetPlan.taskCount
        || packetPlan.taskOffset > m_packetTasks.size()
        || packetPlan.taskCount > m_packetTasks.size() - packetPlan.taskOffset
    )
        return false;

    const usize firstTaskIndex = m_compiledTaskIndexByTask[tasks[0u].index];
    const usize packetEnd = static_cast<usize>(packetPlan.taskOffset) + packetPlan.taskCount;
    if(firstTaskIndex < packetPlan.taskOffset || firstTaskIndex >= packetEnd || taskCount > packetEnd - firstTaskIndex)
        return false;

    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        if(m_packetTasks[firstTaskIndex + taskIndex] != tasks[taskIndex])
            return false;
    }
    return true;
}

bool GpuCompiledGraph::taskJoinsAcceptedQueueFrontier(const GpuTaskId& task)const noexcept{
    const GpuCompiledTask* const compiledTask = findTask(task);
    return compiledTask
        && validPacket(compiledTask->packet)
        && m_packets[compiledTask->packet.index].joinsAcceptedQueueFrontier
    ;
}

const GpuPhysicalQueueInfo* GpuCompiledGraph::queueInfoForTask(const GpuTaskId& task)const noexcept{
    const GpuCompiledTask* const compiledTask = findTask(task);
    return compiledTask ? queueInfo(compiledTask->queue) : nullptr;
}

const GpuCompiledOwnershipTransfer* GpuCompiledGraph::logicalOwnershipTransfers()const noexcept{
    return valid() && !m_ownershipTransfers.empty() ? m_ownershipTransfers.data() : nullptr;
}

const GpuCompiledOwnershipTransfer* GpuCompiledGraph::logicalOwnershipTransferAt(const usize index)const noexcept{
    return valid() && index < m_ownershipTransfers.size() ? &m_ownershipTransfers[index] : nullptr;
}

const GpuCompiledExternalResourceExport* GpuCompiledGraph::externalResourceExport(
    const GpuGraphResourceId& resource
)const noexcept{
    if(!resource.valid() || resource.generation != m_generation)
        return nullptr;
    for(const GpuCompiledExternalResourceExport& exportInfo : m_externalResourceExports){
        if(exportInfo.resource == resource)
            return &exportInfo;
    }
    return nullptr;
}

const GpuCompiledExternalResourceExport* GpuCompiledGraph::externalResourceExportAt(
    const usize index
)const noexcept{
    return index < m_externalResourceExports.size() ? &m_externalResourceExports[index] : nullptr;
}

const GpuCompiledExternalResourceExportSource* GpuCompiledGraph::externalResourceExportSources(
    const GpuCompiledExternalResourceExport& exportInfo
)const noexcept{
    if(
        exportInfo.sourceCount == 0u
        || exportInfo.sourceOffset > m_externalResourceExportSources.size()
        || exportInfo.sourceCount > m_externalResourceExportSources.size() - exportInfo.sourceOffset
    )
        return nullptr;
    return m_externalResourceExportSources.data() + exportInfo.sourceOffset;
}

GpuTaskGraphPhysicalQueueCompileStatistics GpuCompiledGraph::physicalQueueCompileStatistics(
    const GpuPhysicalQueueId& queue
)const noexcept{
    if(!valid() || !queue.valid() || queue.deviceGeneration != m_deviceGeneration)
        return {};

    for(const GpuTaskGraphPhysicalQueueCompileStatistics& statistics : m_physicalQueueCompileStatistics){
        if(statistics.queue == queue)
            return statistics.queueClass < CommandQueue::kCount ? statistics : GpuTaskGraphPhysicalQueueCompileStatistics{};
    }
    return {};
}

const GpuPhysicalQueueInfo* GpuCompiledGraph::queueInfo(const GpuPhysicalQueueId& queue)const noexcept{
    if(!queue.valid() || queue.deviceGeneration != m_deviceGeneration)
        return nullptr;
    for(const GpuPhysicalQueueInfo& info : m_queueTopology){
        if(info.id == queue)
            return &info;
    }
    return nullptr;
}

GpuPhysicalQueueTopology GpuCompiledGraph::queueTopology()const noexcept{
    if(!valid())
        return {};
    return GpuPhysicalQueueTopology{
        .queues = m_queueTopology.empty() ? nullptr : m_queueTopology.data(),
        .queueCount = m_queueTopology.size(),
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

