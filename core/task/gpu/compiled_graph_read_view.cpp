// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiled_graph.h"

#include "task_graph.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

