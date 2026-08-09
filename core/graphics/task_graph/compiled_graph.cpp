// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiled_graph.h"

#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCompiledGraph::GpuCompiledGraph(GraphicsArena& arena)
    : m_tasks(arena)
    , m_packets(arena)
    , m_packetTasks(arena)
    , m_packetDependencies(arena)
    , m_packetExternalDependencies(arena)
    , m_prologueStateSeeds(arena)
    , m_prologueBarriers(arena)
    , m_epilogueBarriers(arena)
    , m_queueTopology(arena)
{}


void GpuCompiledGraph::reset(){
    m_tasks.clear();
    m_packets.clear();
    m_packetTasks.clear();
    m_packetDependencies.clear();
    m_packetExternalDependencies.clear();
    m_prologueStateSeeds.clear();
    m_prologueBarriers.clear();
    m_epilogueBarriers.clear();
    m_queueTopology.clear();
    m_generation = 0u;
    m_deviceGeneration = 0u;
    m_graphTaskCount = 0u;
    m_valid = false;
}

bool GpuCompiledGraph::validFor(const GpuTaskGraph& graph)const noexcept{
    return m_valid
        && m_generation == graph.generation()
        && m_graphTaskCount == graph.taskCount()
        && m_tasks.size() == m_graphTaskCount
        && m_packets.size() == m_graphTaskCount
    ;
}

bool GpuCompiledGraph::validPacket(const GpuSubmissionPacketId& packetID)const noexcept{
    return packetID.valid() && packetID.generation == m_generation && packetID.index < m_packets.size();
}

GpuSubmissionPacketId GpuCompiledGraph::packetIdAt(const usize index)const noexcept{
    return index < m_packets.size()
        ? GpuSubmissionPacketId{ static_cast<u32>(index), m_generation }
        : GpuSubmissionPacketId{}
    ;
}

const GpuCompiledTask* GpuCompiledGraph::findTask(const GpuTaskId& task)const noexcept{
    if(!task.valid() || task.generation != m_generation)
        return nullptr;
    for(const GpuCompiledTask& compiledTask : m_tasks){
        if(compiledTask.task == task)
            return &compiledTask;
    }
    return nullptr;
}

GpuSubmissionPacketId GpuCompiledGraph::packetForTask(const GpuTaskId& task)const noexcept{
    const GpuCompiledTask* const compiledTask = findTask(task);
    return compiledTask ? compiledTask->packet : GpuSubmissionPacketId{};
}

const GpuSubmissionPacket& GpuCompiledGraph::packet(const GpuSubmissionPacketId& packetID)const noexcept{
    NWB_ASSERT(validPacket(packetID));
    return m_packets[packetID.index];
}

const GpuTaskId* GpuCompiledGraph::packetTasks(const GpuSubmissionPacketId& packetID)const noexcept{
    const GpuSubmissionPacket& packetPlan = packet(packetID);
    return packetPlan.taskCount > 0u ? m_packetTasks.data() + packetPlan.taskOffset : nullptr;
}

const GpuPacketDependency* GpuCompiledGraph::packetDependencies(const GpuSubmissionPacketId& packetID)const noexcept{
    const GpuSubmissionPacket& packetPlan = packet(packetID);
    return packetPlan.dependencyCount > 0u ? m_packetDependencies.data() + packetPlan.dependencyOffset : nullptr;
}

const GpuExternalCompletionId* GpuCompiledGraph::packetExternalDependencies(
    const GpuSubmissionPacketId& packetID
)const noexcept{
    const GpuSubmissionPacket& packetPlan = packet(packetID);
    return packetPlan.externalDependencyCount > 0u
        ? m_packetExternalDependencies.data() + packetPlan.externalDependencyOffset
        : nullptr
    ;
}

const GpuPacketStateSeed* GpuCompiledGraph::taskPrologueStateSeeds(const GpuTaskId& task)const noexcept{
    const GpuCompiledTask* const compiledTask = findTask(task);
    if(
        !compiledTask
        || compiledTask->prologueStateSeedCount == 0u
        || compiledTask->prologueStateSeedOffset > m_prologueStateSeeds.size()
        || compiledTask->prologueStateSeedCount > m_prologueStateSeeds.size() - compiledTask->prologueStateSeedOffset
    )
        return nullptr;
    return m_prologueStateSeeds.data() + compiledTask->prologueStateSeedOffset;
}

const GpuCompiledBarrier* GpuCompiledGraph::taskPrologueBarriers(const GpuTaskId& task)const noexcept{
    const GpuCompiledTask* const compiledTask = findTask(task);
    if(
        !compiledTask
        || compiledTask->prologueBarrierCount == 0u
        || compiledTask->prologueBarrierOffset > m_prologueBarriers.size()
        || compiledTask->prologueBarrierCount > m_prologueBarriers.size() - compiledTask->prologueBarrierOffset
    )
        return nullptr;
    return m_prologueBarriers.data() + compiledTask->prologueBarrierOffset;
}

const GpuCompiledBarrier* GpuCompiledGraph::taskEpilogueBarriers(const GpuTaskId& task)const noexcept{
    const GpuCompiledTask* const compiledTask = findTask(task);
    if(
        !compiledTask
        || compiledTask->epilogueBarrierCount == 0u
        || compiledTask->epilogueBarrierOffset > m_epilogueBarriers.size()
        || compiledTask->epilogueBarrierCount > m_epilogueBarriers.size() - compiledTask->epilogueBarrierOffset
    )
        return nullptr;
    return m_epilogueBarriers.data() + compiledTask->epilogueBarrierOffset;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
