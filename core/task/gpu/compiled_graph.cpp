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


thread_local GpuCompiledGraph::ReadView* GpuCompiledGraph::ReadView::s_activeView = nullptr;


GpuCompiledGraph::ReadView::ReadView(const GpuCompiledGraph& graph)noexcept{
    if(s_activeView){
        if(s_activeView->m_graph != &graph)
            return;
        m_graph = &graph;
        m_previousView = s_activeView;
        s_activeView = this;
        return;
    }

    u32 planAccessState = graph.m_planAccessState.load(MemoryOrder::acquire);
    while(true){
        if(
            (planAccessState & GpuCompiledGraph::s_PlanAccessWriterBit) != 0u
            || (planAccessState & GpuCompiledGraph::s_PlanAccessReaderMask)
                == GpuCompiledGraph::s_PlanAccessReaderMask
        )
            return;

        if(graph.m_planAccessState.compare_exchange_weak(
            planAccessState,
            planAccessState + 1u,
            MemoryOrder::acq_rel,
            MemoryOrder::acquire
        ))
            break;
    }

    if(!graph.valid()){
        graph.m_planAccessState.fetch_sub(1u, MemoryOrder::release);
        return;
    }

    m_graph = &graph;
    m_previousView = s_activeView;
    m_ownsAdmission = true;
    s_activeView = this;
}
GpuCompiledGraph::ReadView::~ReadView()noexcept{
    release();
}


void GpuCompiledGraph::ReadView::release()noexcept{
    if(!m_graph)
        return;

    NWB_FATAL_ASSERT_MSG(s_activeView == this, "GpuCompiledGraph read views must unwind in lexical order");
    if(s_activeView != this)
        TerminateInvariant();
    s_activeView = m_previousView;
    if(m_ownsAdmission){
        u32 planAccessState = m_graph->m_planAccessState.load(MemoryOrder::acquire);
        while(true){
            if(
                (planAccessState & GpuCompiledGraph::s_PlanAccessWriterBit) != 0u
                || (planAccessState & GpuCompiledGraph::s_PlanAccessReaderMask) == 0u
            ){
                NWB_FATAL_ASSERT_MSG(false, "GpuCompiledGraph plan read admission must retain its exact reader claim");
                TerminateInvariant();
            }
            if(m_graph->m_planAccessState.compare_exchange_weak(
                planAccessState,
                planAccessState - 1u,
                MemoryOrder::release,
                MemoryOrder::relaxed
            ))
                break;
        }
    }
    m_graph = nullptr;
    m_previousView = nullptr;
    m_ownsAdmission = false;
}


bool GpuCompiledGraph::ReadView::validFor(const GpuTaskGraphDeclarationReadView& graph)const noexcept{
    if(!m_graph || !graph.valid())
        return false;

    const GpuPresentEndpoint* const graphEndpoint = graph.presentEndpoint();
    return m_graph->valid()
        && m_graph->m_generation == graph.generation()
        && m_graph->m_declarationRevision == graph.declarationRevision()
        && m_graph->m_graphTaskCount == graph.taskCount()
        && m_graph->m_tasks.size() == m_graph->m_graphTaskCount
        && m_graph->m_compiledTaskIndexByTask.size() == m_graph->m_graphTaskCount
        && m_graph->m_packetTasks.size() == m_graph->m_graphTaskCount
        && (
            (m_graph->m_graphTaskCount == 0u && m_graph->m_packets.empty())
            || (
                m_graph->m_graphTaskCount > 0u
                && !m_graph->m_packets.empty()
                && m_graph->m_packets.size() <= m_graph->m_graphTaskCount
            )
        )
        && (m_graph->m_hasPresentEndpoint == (graphEndpoint != nullptr))
        && (
            !graphEndpoint
            || (
                m_graph->m_presentEndpoint.valid()
                && m_graph->m_presentEndpoint.producer == graphEndpoint->producer
                && m_graph->m_presentEndpoint.backBuffer == graphEndpoint->backBuffer
                && m_graph->m_presentEndpoint.producer.generation == m_graph->m_generation
                && m_graph->m_presentEndpoint.backBuffer.generation == m_graph->m_generation
                && m_graph->validPacket(m_graph->m_presentEndpoint.packet)
                && m_graph->m_presentEndpoint.packet == m_graph->packetForTask(m_graph->m_presentEndpoint.producer)
                && m_graph->queueInfo(m_graph->m_presentEndpoint.queue) != nullptr
            )
        )
    ;
}

u64 GpuCompiledGraph::ReadView::generation()const noexcept{ return m_graph ? m_graph->generation() : 0u; }
u64 GpuCompiledGraph::ReadView::planGeneration()const noexcept{ return m_graph ? m_graph->planGeneration() : 0u; }
u16 GpuCompiledGraph::ReadView::deviceGeneration()const noexcept{ return m_graph ? m_graph->deviceGeneration() : 0u; }
usize GpuCompiledGraph::ReadView::taskCount()const noexcept{ return m_graph ? m_graph->taskCount() : 0u; }
usize GpuCompiledGraph::ReadView::packetCount()const noexcept{ return m_graph ? m_graph->packetCount() : 0u; }
bool GpuCompiledGraph::ReadView::validPacket(const GpuSubmissionPacketId& packet)const noexcept{
    return m_graph && m_graph->validPacket(packet);
}
bool GpuCompiledGraph::ReadView::validPacketRange(const GpuSubmissionPacketRange& range)const noexcept{
    return m_graph && m_graph->validPacketRange(range);
}
GpuSubmissionPacketId GpuCompiledGraph::ReadView::packetIdAt(const usize index)const noexcept{
    return m_graph ? m_graph->packetIdAt(index) : GpuSubmissionPacketId{};
}
GpuSubmissionPacketRange GpuCompiledGraph::ReadView::packetRange(
    const GpuSubmissionPacketId& first,
    const GpuSubmissionPacketId& last
)const noexcept{
    return m_graph ? m_graph->packetRange(first, last) : GpuSubmissionPacketRange{};
}
GpuSubmissionPacketRange GpuCompiledGraph::ReadView::packetRangeForTasks(
    const GpuTaskId& first,
    const GpuTaskId& last
)const noexcept{
    return m_graph ? m_graph->packetRangeForTasks(first, last) : GpuSubmissionPacketRange{};
}
GpuSubmissionPacketRange GpuCompiledGraph::ReadView::allPacketRange()const noexcept{
    return m_graph ? m_graph->allPacketRange() : GpuSubmissionPacketRange{};
}
GpuSubmissionPacketRange GpuCompiledGraph::ReadView::packetTimingEnvelopeRange()const noexcept{
    return m_graph ? m_graph->packetTimingEnvelopeRange() : GpuSubmissionPacketRange{};
}
GpuCompiledTaskView GpuCompiledGraph::ReadView::findTask(const GpuTaskId& task)const & noexcept{
    if(!m_graph)
        return {};
    const GpuCompiledTask* const plan = m_graph->findTask(task);
    if(
        !plan
        || (
            plan->prologueStateSeedCount != 0u
            && (
                plan->prologueStateSeedOffset > m_graph->m_prologueStateSeeds.size()
                || plan->prologueStateSeedCount
                    > m_graph->m_prologueStateSeeds.size() - plan->prologueStateSeedOffset
            )
        )
        || (
            plan->prologueBarrierCount != 0u
            && (
                plan->prologueBarrierOffset > m_graph->m_prologueBarriers.size()
                || plan->prologueBarrierCount > m_graph->m_prologueBarriers.size() - plan->prologueBarrierOffset
            )
        )
        || (
            plan->epilogueBarrierCount != 0u
            && (
                plan->epilogueBarrierOffset > m_graph->m_epilogueBarriers.size()
                || plan->epilogueBarrierCount > m_graph->m_epilogueBarriers.size() - plan->epilogueBarrierOffset
            )
        )
    )
        return {};
    return GpuCompiledTaskView{
        .plan = plan,
        .prologueStateSeeds = plan->prologueStateSeedCount != 0u
            ? m_graph->m_prologueStateSeeds.data() + plan->prologueStateSeedOffset
            : nullptr,
        .prologueBarriers = plan->prologueBarrierCount != 0u
            ? m_graph->m_prologueBarriers.data() + plan->prologueBarrierOffset
            : nullptr,
        .epilogueBarriers = plan->epilogueBarrierCount != 0u
            ? m_graph->m_epilogueBarriers.data() + plan->epilogueBarrierOffset
            : nullptr,
    };
}
GpuSubmissionPacketId GpuCompiledGraph::ReadView::packetForTask(const GpuTaskId& task)const noexcept{
    return m_graph ? m_graph->packetForTask(task) : GpuSubmissionPacketId{};
}
GpuTaskPacketizationDecision::Enum GpuCompiledGraph::ReadView::packetizationDecisionForTask(
    const GpuTaskId& task
)const noexcept{
    return m_graph ? m_graph->packetizationDecisionForTask(task) : GpuTaskPacketizationDecision::Unknown;
}
bool GpuCompiledGraph::ReadView::tasksSharePacket(const GpuTaskId& first, const GpuTaskId& second)const noexcept{
    return m_graph && m_graph->tasksSharePacket(first, second);
}
bool GpuCompiledGraph::ReadView::taskPrecedesOrSharesPacket(
    const GpuTaskId& first,
    const GpuTaskId& second
)const noexcept{
    return m_graph && m_graph->taskPrecedesOrSharesPacket(first, second);
}
bool GpuCompiledGraph::ReadView::taskPrecedesInSamePacket(
    const GpuTaskId& first,
    const GpuTaskId& second
)const noexcept{
    return m_graph && m_graph->taskPrecedesInSamePacket(first, second);
}
bool GpuCompiledGraph::ReadView::tasksFormContiguousPacketSequence(
    const GpuTaskId* const tasks,
    const usize taskCount
)const noexcept{
    return m_graph && m_graph->tasksFormContiguousPacketSequence(tasks, taskCount);
}
bool GpuCompiledGraph::ReadView::taskJoinsAcceptedQueueFrontier(const GpuTaskId& task)const noexcept{
    return m_graph && m_graph->taskJoinsAcceptedQueueFrontier(task);
}
const GpuPhysicalQueueInfo* GpuCompiledGraph::ReadView::queueInfoForTask(const GpuTaskId& task)const & noexcept{
    return m_graph ? m_graph->queueInfoForTask(task) : nullptr;
}
GpuCompiledPacketView GpuCompiledGraph::ReadView::packet(const GpuSubmissionPacketId& packet)const & noexcept{
    if(!m_graph || !m_graph->validPacket(packet))
        return {};
    const GpuSubmissionPacket& plan = m_graph->m_packets[packet.index];
    if(
        plan.taskOffset > m_graph->m_packetTasks.size()
        || plan.taskCount > m_graph->m_packetTasks.size() - plan.taskOffset
        || plan.dependencyOffset > m_graph->m_packetDependencies.size()
        || plan.dependencyCount > m_graph->m_packetDependencies.size() - plan.dependencyOffset
        || plan.externalDependencyOffset > m_graph->m_packetExternalDependencies.size()
        || plan.externalDependencyCount
            > m_graph->m_packetExternalDependencies.size() - plan.externalDependencyOffset
    )
        return {};
    return GpuCompiledPacketView{
        .id = packet,
        .plan = &plan,
        .tasks = plan.taskCount != 0u ? m_graph->m_packetTasks.data() + plan.taskOffset : nullptr,
        .dependencies = plan.dependencyCount != 0u
            ? m_graph->m_packetDependencies.data() + plan.dependencyOffset
            : nullptr,
        .externalDependencies = plan.externalDependencyCount != 0u
            ? m_graph->m_packetExternalDependencies.data() + plan.externalDependencyOffset
            : nullptr,
    };
}
usize GpuCompiledGraph::ReadView::logicalOwnershipTransferCount()const noexcept{
    return m_graph ? m_graph->m_ownershipTransfers.size() : 0u;
}
const GpuCompiledOwnershipTransfer* GpuCompiledGraph::ReadView::logicalOwnershipTransfers()const & noexcept{
    return m_graph ? m_graph->logicalOwnershipTransfers() : nullptr;
}
const GpuCompiledOwnershipTransfer* GpuCompiledGraph::ReadView::logicalOwnershipTransferAt(
    const usize index
)const & noexcept{
    return m_graph ? m_graph->logicalOwnershipTransferAt(index) : nullptr;
}
GpuCompiledExternalResourceExportView GpuCompiledGraph::ReadView::externalResourceExport(
    const GpuGraphResourceId& resource
)const & noexcept{
    if(!m_graph)
        return {};
    const GpuCompiledExternalResourceExport* const plan = m_graph->externalResourceExport(resource);
    if(!plan)
        return {};
    return GpuCompiledExternalResourceExportView{
        .plan = plan,
        .sources = m_graph->externalResourceExportSources(*plan),
    };
}
usize GpuCompiledGraph::ReadView::externalResourceExportCount()const noexcept{
    return m_graph ? m_graph->m_externalResourceExports.size() : 0u;
}
const GpuCompiledPresentEndpoint* GpuCompiledGraph::ReadView::presentEndpoint()const & noexcept{
    return m_graph && m_graph->valid() && m_graph->m_hasPresentEndpoint ? &m_graph->m_presentEndpoint : nullptr;
}
GpuTaskGraphCompileStatistics GpuCompiledGraph::ReadView::compileStatistics()const noexcept{
    return m_graph ? m_graph->m_compileStatistics : GpuTaskGraphCompileStatistics{};
}
GpuCompiledExternalResourceExportView GpuCompiledGraph::ReadView::externalResourceExportAt(
    const usize index
)const & noexcept{
    if(!m_graph)
        return {};
    const GpuCompiledExternalResourceExport* const plan = m_graph->externalResourceExportAt(index);
    if(!plan)
        return {};
    return GpuCompiledExternalResourceExportView{
        .plan = plan,
        .sources = m_graph->externalResourceExportSources(*plan),
    };
}
GpuTaskGraphPhysicalQueueCompileStatistics GpuCompiledGraph::ReadView::physicalQueueCompileStatistics(
    const GpuPhysicalQueueId& queue
)const noexcept{
    return m_graph ? m_graph->physicalQueueCompileStatistics(queue) : GpuTaskGraphPhysicalQueueCompileStatistics{};
}
const GpuPhysicalQueueInfo* GpuCompiledGraph::ReadView::queueInfo(const GpuPhysicalQueueId& queue)const & noexcept{
    return m_graph ? m_graph->queueInfo(queue) : nullptr;
}
GpuPhysicalQueueTopology GpuCompiledGraph::ReadView::queueTopology()const & noexcept{
    return m_graph ? m_graph->queueTopology() : GpuPhysicalQueueTopology{};
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
        ? GpuSubmissionPacketId{ static_cast<u32>(index), m_planGeneration }
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

    bool foundFirst = false;
    for(u32 taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        const GpuTaskId task = m_packetTasks[packetPlan.taskOffset + taskIndex];
        if(task == first)
            foundFirst = true;
        else if(task == second)
            return foundFirst;
    }
    return false;
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

    for(u32 firstTaskIndex = 0u;
        static_cast<usize>(firstTaskIndex) + taskCount <= packetPlan.taskCount;
        ++firstTaskIndex
    ){
        bool matches = true;
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
            if(m_packetTasks[packetPlan.taskOffset + firstTaskIndex + taskIndex] == tasks[taskIndex])
                continue;
            matches = false;
            break;
        }
        if(matches)
            return true;
    }
    return false;
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
    if(!valid())
        return {};

    const GpuPhysicalQueueInfo* const queueInfo = this->queueInfo(queue);
    if(!queueInfo || queueInfo->queueClass >= CommandQueue::kCount)
        return {};

    GpuTaskGraphPhysicalQueueCompileStatistics statistics{
        .graphGeneration = m_generation,
        .planGeneration = m_planGeneration,
        .queue = queue,
        .deviceGeneration = m_deviceGeneration,
        .queueClass = queueInfo->queueClass,
    };
    const auto countOwnershipBarriers = [&statistics](
        const GraphicsVector<GpuCompiledBarrier>& barriers,
        const u32 barrierOffset,
        const u32 barrierCount
    ){
        if(
            barrierCount == 0u
            || barrierOffset > barriers.size()
            || barrierCount > barriers.size() - barrierOffset
        )
            return;

        const GpuCompiledBarrier* const taskBarriers = barriers.data() + barrierOffset;
        for(u32 barrierIndex = 0u; barrierIndex < barrierCount; ++barrierIndex){
            switch(taskBarriers[barrierIndex].type){
            case GpuCompiledBarrierType::TextureOwnershipRelease:
            case GpuCompiledBarrierType::BufferOwnershipRelease:
            case GpuCompiledBarrierType::AccelStructOwnershipRelease:
                ++statistics.ownershipReleaseBarrierCount;
                break;
            case GpuCompiledBarrierType::TextureOwnershipAcquire:
            case GpuCompiledBarrierType::BufferOwnershipAcquire:
            case GpuCompiledBarrierType::AccelStructOwnershipAcquire:
                ++statistics.ownershipAcquireBarrierCount;
                break;
            default:
                break;
            }
        }
    };
    for(const GpuCompiledTask& task : m_tasks){
        if(task.queue != queue)
            continue;

        ++statistics.taskCount;
        statistics.prologueBarrierCount += task.prologueBarrierCount;
        statistics.epilogueBarrierCount += task.epilogueBarrierCount;
        countOwnershipBarriers(m_prologueBarriers, task.prologueBarrierOffset, task.prologueBarrierCount);
        countOwnershipBarriers(m_epilogueBarriers, task.epilogueBarrierOffset, task.epilogueBarrierCount);
    }
    for(const GpuSubmissionPacket& packet : m_packets){
        if(packet.queue != queue)
            continue;

        ++statistics.packetCount;
        if(packet.taskCount > 1u)
            statistics.mergedTaskCount += packet.taskCount - 1u;
    }
    const auto sameTransferSignature = [](const GpuCompiledOwnershipTransfer& lhs, const GpuCompiledOwnershipTransfer& rhs){
        return lhs.resource == rhs.resource
            && lhs.route == rhs.route
            && lhs.sourceTask == rhs.sourceTask
            && lhs.destinationTask == rhs.destinationTask
            && lhs.sourceQueue == rhs.sourceQueue
            && lhs.destinationQueue == rhs.destinationQueue
        ;
    };
    for(usize transferIndex = 0u; transferIndex < m_ownershipTransfers.size(); ++transferIndex){
        const GpuCompiledOwnershipTransfer& transfer = m_ownershipTransfers[transferIndex];
        if(transfer.sourceQueue == queue)
            ++statistics.outgoingLogicalOwnershipTransferCount;
        if(transfer.destinationQueue == queue)
            ++statistics.incomingLogicalOwnershipTransferCount;

        bool signatureAlreadyCounted = false;
        bool hasEarlierDistinctSignature = false;
        for(usize previousIndex = 0u; previousIndex < transferIndex; ++previousIndex){
            const GpuCompiledOwnershipTransfer& previous = m_ownershipTransfers[previousIndex];
            if(sameTransferSignature(transfer, previous)){
                signatureAlreadyCounted = true;
                break;
            }
            if(previous.resource == transfer.resource)
                hasEarlierDistinctSignature = true;
        }
        if(signatureAlreadyCounted)
            continue;

        if(transfer.sourceQueue == queue){
            ++statistics.outgoingLogicalOwnershipTransferSignatureCount;
            if(hasEarlierDistinctSignature)
                ++statistics.outgoingRepeatedOwnershipTransferSignatureCount;
        }
        if(transfer.destinationQueue == queue){
            ++statistics.incomingLogicalOwnershipTransferSignatureCount;
            if(hasEarlierDistinctSignature)
                ++statistics.incomingRepeatedOwnershipTransferSignatureCount;
        }
    }
    for(usize transferIndex = 0u; transferIndex < m_ownershipTransfers.size(); ++transferIndex){
        const GpuCompiledOwnershipTransfer& transfer = m_ownershipTransfers[transferIndex];
        if(
            !transfer.concurrentSharingCouldAvoid
            || (transfer.sourceQueue != queue && transfer.destinationQueue != queue)
        )
            continue;

        bool resourceAlreadyCounted = false;
        for(usize previousIndex = 0u; previousIndex < transferIndex; ++previousIndex){
            const GpuCompiledOwnershipTransfer& previous = m_ownershipTransfers[previousIndex];
            if(
                previous.resource == transfer.resource
                && previous.concurrentSharingCouldAvoid
                && (previous.sourceQueue == queue || previous.destinationQueue == queue)
            ){
                resourceAlreadyCounted = true;
                break;
            }
        }
        if(resourceAlreadyCounted)
            continue;

        usize distinctSignatureCount = 0u;
        for(usize candidateIndex = 0u; candidateIndex < m_ownershipTransfers.size(); ++candidateIndex){
            const GpuCompiledOwnershipTransfer& candidate = m_ownershipTransfers[candidateIndex];
            if(candidate.resource != transfer.resource || !candidate.concurrentSharingCouldAvoid)
                continue;

            bool signatureAlreadyCounted = false;
            for(usize previousIndex = 0u; previousIndex < candidateIndex; ++previousIndex){
                if(sameTransferSignature(candidate, m_ownershipTransfers[previousIndex])){
                    signatureAlreadyCounted = true;
                    break;
                }
            }
            if(!signatureAlreadyCounted)
                ++distinctSignatureCount;
        }
        if(distinctSignatureCount > 1u)
            ++statistics.concurrentSharingAdviceResourceCount;
    }
    return statistics;
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

bool GpuCompiledGraph::beginRecordingAttempt(
    const GpuTaskGraph& graph,
    const GpuTaskGraphDeclarationReadView& declarations,
    const GpuSubmissionPacketId packet,
    const u64 expectedPlanGeneration,
    const u64 recordingAttemptGeneration,
    const u64 previousRecordingAttemptGeneration,
    const ReadView& planAccess
)const noexcept{
    if(!packet.valid() || expectedPlanGeneration == 0u || recordingAttemptGeneration == 0u)
        return false;

    NothrowScopedLock lock(m_attemptBindingMutex);
    if(
        !planAccess.validFor(*this)
        || !declarations.validFor(graph)
        || !planAccess.validFor(declarations)
        || m_planGeneration != expectedPlanGeneration
        || !validPacket(packet)
    )
        return false;

    const bool exactAttempt = m_attemptGraph == &graph
        && m_attemptPlanGeneration == expectedPlanGeneration
        && m_attemptRecordingGeneration == recordingAttemptGeneration
    ;
    if(
        m_attemptBindingState == AttemptBindingState::Recording
        || m_attemptBindingState == AttemptBindingState::Submitting
    ){
        if(exactAttempt)
            return true;
        if(
            m_attemptBindingState != AttemptBindingState::Recording
            || previousRecordingAttemptGeneration == 0u
            || m_attemptGraph != &graph
            || m_attemptPlanGeneration != expectedPlanGeneration
            || m_attemptRecordingGeneration != previousRecordingAttemptGeneration
        )
            return false;
    }
    else if(
        m_attemptBindingState != AttemptBindingState::None
        && m_attemptBindingState != AttemptBindingState::Resolved
    )
        return false;

    m_attemptGraph = &graph;
    m_attemptPlanGeneration = expectedPlanGeneration;
    m_attemptRecordingGeneration = recordingAttemptGeneration;
    m_attemptTransactionIdentity = 0u;
    m_attemptTransactionResetGeneration = 0u;
    m_attemptBindingState = AttemptBindingState::Recording;
    return true;
}


bool GpuCompiledGraph::matchesRecordingAttempt(
    const GpuTaskGraph& graph,
    const u64 expectedPlanGeneration,
    const u64 recordingAttemptGeneration
)const noexcept{
    if(expectedPlanGeneration == 0u || recordingAttemptGeneration == 0u)
        return false;

    NothrowScopedLock lock(m_attemptBindingMutex);
    return (
            m_attemptBindingState == AttemptBindingState::Recording
            || m_attemptBindingState == AttemptBindingState::Submitting
        )
        && m_attemptGraph == &graph
        && m_attemptPlanGeneration == expectedPlanGeneration
        && m_attemptRecordingGeneration == recordingAttemptGeneration
        && m_planGeneration == expectedPlanGeneration
    ;
}


bool GpuCompiledGraph::resolveRecordingAttempt(
    const GpuTaskGraph& graph,
    const u64 expectedPlanGeneration,
    const u64 recordingAttemptGeneration
)const noexcept{
    if(expectedPlanGeneration == 0u || recordingAttemptGeneration == 0u)
        return false;

    NothrowScopedLock lock(m_attemptBindingMutex);
    if(
        m_attemptGraph != &graph
        || m_attemptPlanGeneration != expectedPlanGeneration
        || m_attemptRecordingGeneration != recordingAttemptGeneration
    )
        return false;
    if(m_attemptBindingState == AttemptBindingState::Resolved)
        return true;
    if(m_attemptBindingState != AttemptBindingState::Recording)
        return false;
    m_attemptBindingState = AttemptBindingState::Resolved;
    return true;
}


bool GpuCompiledGraph::bindSubmissionTransaction(
    const GpuTaskGraph& graph,
    const u64 expectedPlanGeneration,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)const noexcept{
    if(!submissionBinding.valid() || expectedPlanGeneration == 0u || recordingAttemptGeneration == 0u)
        return false;

    NothrowScopedLock lock(m_attemptBindingMutex);
    if(m_attemptBindingState == AttemptBindingState::Submitting){
        return m_attemptGraph == &graph
            && m_attemptPlanGeneration == expectedPlanGeneration
            && m_attemptRecordingGeneration == recordingAttemptGeneration
            && m_attemptTransactionIdentity == submissionBinding.m_transactionIdentity
            && m_attemptTransactionResetGeneration == submissionBinding.m_resetGeneration
            && m_planGeneration == expectedPlanGeneration
        ;
    }
    if(
        m_attemptBindingState != AttemptBindingState::Recording
        || m_attemptGraph != &graph
        || m_attemptPlanGeneration != expectedPlanGeneration
        || m_attemptRecordingGeneration != recordingAttemptGeneration
    )
        return false;
    if(m_planGeneration != expectedPlanGeneration)
        return false;
    m_attemptTransactionIdentity = submissionBinding.m_transactionIdentity;
    m_attemptTransactionResetGeneration = submissionBinding.m_resetGeneration;
    m_attemptBindingState = AttemptBindingState::Submitting;
    return true;
}


bool GpuCompiledGraph::matchesSubmissionTransaction(
    const GpuTaskGraph& graph,
    const u64 expectedPlanGeneration,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)const noexcept{
    if(!submissionBinding.valid() || expectedPlanGeneration == 0u || recordingAttemptGeneration == 0u)
        return false;

    NothrowScopedLock lock(m_attemptBindingMutex);
    return m_attemptGraph == &graph
        && m_attemptPlanGeneration == expectedPlanGeneration
        && m_attemptBindingState != AttemptBindingState::None
        && m_attemptBindingState != AttemptBindingState::Recording
        && m_attemptRecordingGeneration == recordingAttemptGeneration
        && m_attemptTransactionIdentity == submissionBinding.m_transactionIdentity
        && m_attemptTransactionResetGeneration == submissionBinding.m_resetGeneration
        && m_planGeneration == expectedPlanGeneration
    ;
}


bool GpuCompiledGraph::resolveSubmissionTransaction(
    const GpuTaskGraph& graph,
    const u64 expectedPlanGeneration,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)const noexcept{
    if(!submissionBinding.valid() || expectedPlanGeneration == 0u || recordingAttemptGeneration == 0u)
        return false;

    NothrowScopedLock lock(m_attemptBindingMutex);
    if(
        m_attemptGraph != &graph
        || m_attemptPlanGeneration != expectedPlanGeneration
        || m_attemptRecordingGeneration != recordingAttemptGeneration
        || m_attemptTransactionIdentity != submissionBinding.m_transactionIdentity
        || m_attemptTransactionResetGeneration != submissionBinding.m_resetGeneration
    )
        return false;
    if(m_attemptBindingState == AttemptBindingState::Resolved)
        return true;
    if(m_attemptBindingState != AttemptBindingState::Submitting)
        return false;
    m_attemptBindingState = AttemptBindingState::Resolved;
    return true;
}


bool GpuCompiledGraph::matchesActiveAttemptIdentity(
    const GpuTaskGraph* const graphIdentity,
    const u64 compiledObjectIdentity,
    const u64 expectedPlanGeneration,
    const u64 recordingAttemptGeneration,
    const ReadView& planAccess
)const noexcept{
    if(
        !graphIdentity
        || compiledObjectIdentity == 0u
        || expectedPlanGeneration == 0u
        || recordingAttemptGeneration == 0u
    )
        return false;

    NothrowScopedLock lock(m_attemptBindingMutex);
    return planAccess.validFor(*this)
        && m_objectIdentity == compiledObjectIdentity
        && (
            m_attemptBindingState == AttemptBindingState::Recording
            || m_attemptBindingState == AttemptBindingState::Submitting
        )
        && m_attemptGraph == graphIdentity
        && m_attemptPlanGeneration == expectedPlanGeneration
        && m_attemptRecordingGeneration == recordingAttemptGeneration
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

