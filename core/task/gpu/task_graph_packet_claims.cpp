// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_packet_claims.h"

#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuRecordingAttemptScope::~GpuRecordingAttemptScope()noexcept{
    if(m_graph)
        m_graph->cancelRecordingAttempt(*this);
}


void GpuRecordingAttemptScope::complete()noexcept{
    if(m_graph){
        m_graph->completeRecordingPreparation(*this);
        return;
    }
    completeWithinLock();
}


void GpuRecordingAttemptScope::activateWithinLock(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const u64 recordingAttemptGeneration, const u64 preparationSerial, const bool previousPlanWasActive)noexcept{
    NWB_FATAL_ASSERT_MSG(!m_graph, "GPU task graph recording attempt scope must be fresh");
    NWB_FATAL_ASSERT_MSG(preparationSerial != 0u, "GPU task graph recording preparation requires a nonzero serial");
    m_graph = &graph;
    m_compiledGraph = &compiledGraph;
    m_recordingAttemptGeneration = recordingAttemptGeneration;
    m_preparationSerial = preparationSerial;
    m_previousPlanWasActive = previousPlanWasActive;
}


void GpuRecordingAttemptScope::completeWithinLock()noexcept{
    m_graph = nullptr;
    m_compiledGraph = nullptr;
    m_recordingAttemptGeneration = 0u;
    m_preparationSerial = 0u;
    m_previousPlanWasActive = false;
}


bool GpuRecordingAttemptScope::validPreparationWithinLock(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const u64 recordingAttemptGeneration, const u64 preparationSerial)const noexcept{
    return m_graph == &graph && m_compiledGraph == &compiledGraph && m_recordingAttemptGeneration == recordingAttemptGeneration && m_preparationSerial != 0u && m_preparationSerial == preparationSerial;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuPacketRecordingAbort::GpuPacketRecordingAbort(GpuPacketRecordingAbort&& other)noexcept
    : m_packet(other.m_packet)
    , m_planGeneration(other.m_planGeneration)
    , m_recordingAttemptGeneration(other.m_recordingAttemptGeneration)
    , m_claimGeneration(other.m_claimGeneration)
{
    other.reset();
}

GpuPacketRecordingAbort::~GpuPacketRecordingAbort()noexcept{
    if(valid())
        NWB_FATAL_ASSERT_MSG(false, "GPU task packet recording abort must be consumed before destruction");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuPacketRecordingLease::validFor(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId packet)const noexcept{
    if(!valid() || !planAccess.validFor(compiledGraph) || !planAccess.validPacket(packet) || m_packet != packet || m_planGeneration != planAccess.planGeneration())
        return false;

    const GpuCompiledPacketView packetView = planAccess.packetWithTasks(packet);
    if(!packetView.valid())
        return false;
    const GpuSubmissionPacket& packetPlan = *packetView.plan;
    const GpuTaskId* const tasks = packetView.tasks;

    NothrowScopedLock lock(graph.m_lifecycleMutex);
    if(!graph.matchesPacketAttemptWithinLock(compiledGraph, m_planGeneration, m_recordingAttemptGeneration) || graph.m_activeRecordingPreparationSerial != 0u)
        return false;

    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        if(!graph.validTask(tasks[taskIndex]))
            return false;
        const GpuTaskGraph::GpuTaskNode& task = graph.m_tasks[tasks[taskIndex].index];
        if(task.lifecycleState != GpuTaskGraph::TaskLifecycleState::Recording || task.lifecycleAttemptGeneration != m_recordingAttemptGeneration || task.recordingClaimGeneration != m_claimGeneration)
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuPacketRecordingAccess::GpuPacketRecordingAccess(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuPacketRecordingLease& lease)noexcept{
    if(!lease.validFor(graph, compiledGraph, planAccess, lease.m_packet))
        return;

    m_graph = &graph;
    m_compiledGraph = &compiledGraph;
    m_lease = &lease;
    m_packet = lease.m_packet;
    m_planGeneration = lease.m_planGeneration;
    m_recordingAttemptGeneration = lease.m_recordingAttemptGeneration;
    m_claimGeneration = lease.m_claimGeneration;
}

bool GpuPacketRecordingAccess::validFor(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId packet)const noexcept{
    return
        m_graph == &graph
        && m_compiledGraph == &compiledGraph
        && m_lease->m_packet == m_packet
        && m_lease->m_planGeneration == m_planGeneration
        && m_lease->m_recordingAttemptGeneration == m_recordingAttemptGeneration
        && m_lease->m_claimGeneration == m_claimGeneration
        && planAccess.validFor(compiledGraph)
        && m_packet == packet
        && m_planGeneration == planAccess.planGeneration()
    ;
}

bool GpuPacketRecordingAccess::validForTask(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuTaskId& task)const noexcept{
    if(!validFor(graph, compiledGraph, planAccess, m_packet))
        return false;
    const GpuCompiledTaskView compiledTask = planAccess.findTask(task);
    return compiledTask.valid() && compiledTask.plan->packet == m_packet;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

