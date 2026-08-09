// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"

#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuRecordedGraph::reset(const GpuCompiledGraph& compiledGraph){
    m_packets.clear();
    m_generation = compiledGraph.generation();
    m_deviceGeneration = compiledGraph.deviceGeneration();
    m_valid = compiledGraph.valid();
}

bool GpuRecordedGraph::validFor(const GpuCompiledGraph& compiledGraph)const noexcept{
    return m_valid
        && compiledGraph.valid()
        && m_generation == compiledGraph.generation()
        && m_deviceGeneration == compiledGraph.deviceGeneration()
    ;
}

const GpuRecordedPacket* GpuRecordedGraph::find(const GpuSubmissionPacketId& packet)const noexcept{
    if(!packet.valid() || packet.generation != m_generation)
        return nullptr;
    for(const GpuRecordedPacket& recordedPacket : m_packets){
        if(recordedPacket.packet == packet)
            return &recordedPacket;
    }
    return nullptr;
}


bool GpuNativePacketRecorder::recordPacket(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecordDesc& desc,
    GpuRecordedGraph& outRecordedGraph
)const{
    if(!compiledGraph.validFor(graph) || !compiledGraph.validPacket(desc.packet))
        return false;
    if(!outRecordedGraph.validFor(compiledGraph))
        outRecordedGraph.reset(compiledGraph);
    if(outRecordedGraph.find(desc.packet))
        return false;

    const GpuSubmissionPacket& packet = compiledGraph.packet(desc.packet);
    const GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfo(packet.queue);
    if(!queue || queue->queueClass >= CommandQueue::kCount)
        return false;

    CommandListParameters parameters;
    parameters.setQueueType(queue->queueClass);
    CommandListHandle commandList = m_device.createCommandList(parameters);
    if(!commandList)
        return false;

    commandList->open(desc.initialStates);
    bool recorded = commandList->hasCommandBuffer();
    const GpuTaskId* const tasks = compiledGraph.packetTasks(desc.packet);
    if(!tasks || packet.taskCount == 0u)
        recorded = false;
    for(u32 taskIndex = 0u; recorded && taskIndex < packet.taskCount; ++taskIndex){
        const GpuTaskId task = tasks[taskIndex];
        const GpuTaskGraphTaskView taskView = graph.taskAt(task.index);
        const GpuCompiledTask* const compiledTask = compiledGraph.findTask(task);
        if(!compiledTask || compiledTask->packet != desc.packet){
            recorded = false;
            break;
        }
        const GpuTaskRecordContext context{
            .graph = compiledGraph,
            .task = task,
            .packet = desc.packet,
            .queue = packet.queue,
        };
        if(desc.applyCompiledBarriers){
            const GpuCompiledBarrier* const prologueBarriers = compiledGraph.taskPrologueBarriers(task);
            if(compiledTask->prologueBarrierCount > 0u && !prologueBarriers){
                recorded = false;
                break;
            }
            for(u32 barrierIndex = 0u; recorded && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex)
                recorded = graph.applyCompiledBarrier(prologueBarriers[barrierIndex], *commandList);
            if(recorded)
                commandList->commitBarriers();
        }
        commandList->beginMarker(taskView.markerLabel);
        if(recorded)
            recorded = graph.recordTask(task, *commandList, context);
        commandList->endMarker();
        if(desc.applyCompiledBarriers){
            const GpuCompiledBarrier* const epilogueBarriers = compiledGraph.taskEpilogueBarriers(task);
            if(compiledTask->epilogueBarrierCount > 0u && !epilogueBarriers)
                recorded = false;
            for(u32 barrierIndex = 0u; recorded && barrierIndex < compiledTask->epilogueBarrierCount; ++barrierIndex)
                recorded = graph.applyCompiledBarrier(epilogueBarriers[barrierIndex], *commandList);
            if(recorded)
                commandList->commitBarriers();
        }
    }
    commandList->close(desc.finalStates);
    if(!recorded || !commandList->hasCommandBuffer())
        return false;

    GpuRecordedPacket recordedPacket;
    recordedPacket.packet = desc.packet;
    recordedPacket.commandLists[0u] = commandList.get();
    recordedPacket.commandListCount = 1u;
    recordedPacket.ownedCommandList = Move(commandList);
    outRecordedGraph.m_packets.push_back(Move(recordedPacket));
    return true;
}


bool GpuNativePacketRecorder::recordImportedPacket(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuImportedPacketRecordDesc& desc,
    GpuRecordedGraph& outRecordedGraph
)const{
    if(
        !compiledGraph.validFor(graph)
        || !compiledGraph.validPacket(desc.packet)
        || !desc.commandLists
        || desc.commandListCount == 0u
        || desc.commandListCount > GpuRecordedPacket::s_MaxCommandLists
    )
        return false;
    if(!outRecordedGraph.validFor(compiledGraph))
        outRecordedGraph.reset(compiledGraph);
    if(outRecordedGraph.find(desc.packet))
        return false;

    GpuRecordedPacket recordedPacket;
    recordedPacket.packet = desc.packet;
    recordedPacket.commandListCount = static_cast<u8>(desc.commandListCount);
    for(usize commandListIndex = 0u; commandListIndex < desc.commandListCount; ++commandListIndex){
        CommandList* const commandList = desc.commandLists[commandListIndex];
        if(!commandList || !commandList->hasCommandBuffer())
            return false;
        recordedPacket.commandLists[commandListIndex] = commandList;
    }
    outRecordedGraph.m_packets.push_back(Move(recordedPacket));
    return true;
}


void GpuGraphSubmissionTransaction::reset(const GpuCompiledGraph& compiledGraph){
    m_packets.clear();
    m_latestAcceptedQueueTokens.clear();
    m_generation = compiledGraph.generation();
    m_deviceGeneration = compiledGraph.deviceGeneration();
    m_valid = compiledGraph.valid();
    if(!m_valid)
        return;
    m_packets.resize(compiledGraph.packetCount());
}

bool GpuGraphSubmissionTransaction::validFor(const GpuCompiledGraph& compiledGraph)const noexcept{
    return m_valid
        && compiledGraph.valid()
        && m_generation == compiledGraph.generation()
        && m_deviceGeneration == compiledGraph.deviceGeneration()
        && m_packets.size() == compiledGraph.packetCount()
    ;
}

bool GpuGraphSubmissionTransaction::markPacketRecorded(const GpuSubmissionPacketId& packet)noexcept{
    if(!m_valid || !packet.valid() || packet.generation != m_generation || packet.index >= m_packets.size())
        return false;
    GpuPacketRuntime& runtime = m_packets[packet.index];
    if(runtime.state != GpuPacketRuntimeState::Declared)
        return false;
    runtime.state = GpuPacketRuntimeState::Recorded;
    return true;
}

void GpuGraphSubmissionTransaction::acceptPacket(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId& packetID,
    const QueueSubmissionToken& token
)noexcept{
    if(!validFor(compiledGraph) || !compiledGraph.validPacket(packetID) || !token.valid())
        return;
    GpuPacketRuntime& runtime = m_packets[packetID.index];
    if(runtime.state != GpuPacketRuntimeState::Recorded)
        return;

    runtime.state = GpuPacketRuntimeState::Accepted;
    runtime.token = token;
    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packetID);
    for(u32 taskIndex = 0u; tasks && taskIndex < packet.taskCount; ++taskIndex)
        graph.acceptTask(tasks[taskIndex], token);

    for(LatestAcceptedQueueToken& latest : m_latestAcceptedQueueTokens){
        if(latest.queue == packet.queue){
            latest.token = token;
            return;
        }
    }
    m_latestAcceptedQueueTokens.push_back(LatestAcceptedQueueToken{
        .queue = packet.queue,
        .token = token,
    });
}

void GpuGraphSubmissionTransaction::rejectPacket(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId& packetID
)noexcept{
    if(!validFor(compiledGraph) || !compiledGraph.validPacket(packetID))
        return;
    GpuPacketRuntime& runtime = m_packets[packetID.index];
    if(runtime.state == GpuPacketRuntimeState::Accepted || runtime.state == GpuPacketRuntimeState::Rejected)
        return;

    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packetID);
    for(u32 taskIndex = 0u; tasks && taskIndex < packet.taskCount; ++taskIndex)
        graph.discardTask(tasks[taskIndex]);
    runtime.state = GpuPacketRuntimeState::Rejected;
}

void GpuGraphSubmissionTransaction::discardUnaccepted(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph
)noexcept{
    if(!validFor(compiledGraph))
        return;
    for(usize packetIndex = 0u; packetIndex < m_packets.size(); ++packetIndex){
        if(m_packets[packetIndex].state == GpuPacketRuntimeState::Accepted)
            continue;
        rejectPacket(graph, compiledGraph, compiledGraph.packetIdAt(packetIndex));
    }
}

QueueSubmissionToken GpuGraphSubmissionTransaction::packetToken(const GpuSubmissionPacketId& packet)const noexcept{
    const GpuPacketRuntime* const runtime = packetRuntime(packet);
    return runtime && runtime->state == GpuPacketRuntimeState::Accepted ? runtime->token : QueueSubmissionToken{};
}

const QueueSubmissionToken* GpuGraphSubmissionTransaction::latestAcceptedToken(
    const GpuPhysicalQueueId& queue
)const noexcept{
    for(const LatestAcceptedQueueToken& latest : m_latestAcceptedQueueTokens){
        if(latest.queue == queue && latest.token.valid())
            return &latest.token;
    }
    return nullptr;
}

const GpuPacketRuntime* GpuGraphSubmissionTransaction::packetRuntime(
    const GpuSubmissionPacketId& packet
)const noexcept{
    if(!m_valid || !packet.valid() || packet.generation != m_generation || packet.index >= m_packets.size())
        return nullptr;
    return &m_packets[packet.index];
}


bool GpuTaskGraphSubmitter::submitPacket(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuSubmissionPacketId& packetID,
    const GpuTaskGraphExternalCompletionToken* const externalCompletionTokens,
    const usize externalCompletionTokenCount,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuTimingSubmissionTicket* const timingTicket
)const{
    if(
        !compiledGraph.validFor(graph)
        || !compiledGraph.validPacket(packetID)
        || !recordedGraph.validFor(compiledGraph)
        || !transaction.validFor(compiledGraph)
        || (externalCompletionTokenCount > 0u && !externalCompletionTokens)
        || !transaction.markPacketRecorded(packetID)
    )
        return false;

    const GpuRecordedPacket* const recordedPacket = recordedGraph.find(packetID);
    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    const GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfo(packet.queue);
    if(
        !recordedPacket
        || recordedPacket->commandListCount == 0u
        || recordedPacket->commandListCount > GpuRecordedPacket::s_MaxCommandLists
        || !queue
    ){
        transaction.rejectPacket(graph, compiledGraph, packetID);
        return false;
    }
    for(u8 commandListIndex = 0u; commandListIndex < recordedPacket->commandListCount; ++commandListIndex){
        CommandList* const commandList = recordedPacket->commandLists[commandListIndex];
        if(!commandList || !commandList->hasCommandBuffer()){
            transaction.rejectPacket(graph, compiledGraph, packetID);
            return false;
        }
    }

    Vector<QueueSubmissionToken, Alloc::ScratchArena> waitTokens(scratchArena);
    waitTokens.reserve(packet.dependencyCount + packet.externalDependencyCount);
    const GpuPacketDependency* const dependencies = compiledGraph.packetDependencies(packetID);
    for(u32 dependencyIndex = 0u; dependencyIndex < packet.dependencyCount; ++dependencyIndex){
        const QueueSubmissionToken token = transaction.packetToken(dependencies[dependencyIndex].producer);
        if(!token.valid()){
            transaction.rejectPacket(graph, compiledGraph, packetID);
            return false;
        }
        waitTokens.push_back(token);
    }

    const GpuExternalCompletionId* const externalDependencies = compiledGraph.packetExternalDependencies(packetID);
    for(u32 dependencyIndex = 0u; dependencyIndex < packet.externalDependencyCount; ++dependencyIndex){
        const GpuExternalCompletionId completion = externalDependencies[dependencyIndex];
        QueueSubmissionToken token;
        for(usize tokenIndex = 0u; tokenIndex < externalCompletionTokenCount; ++tokenIndex){
            const GpuTaskGraphExternalCompletionToken& binding = externalCompletionTokens[tokenIndex];
            if(binding.completion == completion){
                token = binding.token;
                break;
            }
        }
        if(!token.valid()){
            transaction.rejectPacket(graph, compiledGraph, packetID);
            return false;
        }
        waitTokens.push_back(token);
    }

    QueueSubmissionDesc submitDesc;
    if(!waitTokens.empty())
        submitDesc.setWaitTokens(waitTokens.data(), waitTokens.size());
    const QueueSubmissionToken token = timingTicket
        ? timingTicket->submit(
            m_device,
            recordedPacket->commandLists,
            recordedPacket->commandListCount,
            queue->queueClass,
            submitDesc
        )
        : m_device.executeCommandLists(
            recordedPacket->commandLists,
            recordedPacket->commandListCount,
            queue->queueClass,
            submitDesc
        )
    ;
    if(!token.valid()){
        transaction.rejectPacket(graph, compiledGraph, packetID);
        return false;
    }

    transaction.acceptPacket(graph, compiledGraph, packetID, token);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
