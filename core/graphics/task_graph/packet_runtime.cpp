// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuRecordedGraph::reset(const GpuCompiledGraph& compiledGraph){
    m_packets.clear();
    m_packetStateSeeds.clear();
    m_packetStateSeeds.reserve(compiledGraph.packetCount());
    for(usize packetIndex = 0u; packetIndex < compiledGraph.packetCount(); ++packetIndex)
        m_packetStateSeeds.emplace_back(m_arena);
    m_initialStateSeed.reset();
    m_stateSubsetScratch.reset();
    m_stateMergeScratch.reset();
    m_externalBaseStateSeed.reset();
    m_externalMergedStateSeed.reset();
    m_generation = compiledGraph.generation();
    m_deviceGeneration = compiledGraph.deviceGeneration();
    m_valid = compiledGraph.valid();
}

bool GpuRecordedGraph::validFor(const GpuCompiledGraph& compiledGraph)const noexcept{
    return m_valid
        && compiledGraph.valid()
        && m_generation == compiledGraph.generation()
        && m_deviceGeneration == compiledGraph.deviceGeneration()
        && m_packetStateSeeds.size() == compiledGraph.packetCount()
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

const CommandListResourceStateHandoff* GpuRecordedGraph::packetFinalStateSeed(
    const GpuSubmissionPacketId& packet
)const noexcept{
    const CommandListResourceStateHandoff* const stateSeed = packetStateSeed(packet);
    return find(packet) && stateSeed && stateSeed->valid() ? stateSeed : nullptr;
}

CommandListResourceStateHandoff* GpuRecordedGraph::packetStateSeed(const GpuSubmissionPacketId& packet)noexcept{
    if(!packet.valid() || packet.generation != m_generation || packet.index >= m_packetStateSeeds.size())
        return nullptr;
    return &m_packetStateSeeds[packet.index];
}

const CommandListResourceStateHandoff* GpuRecordedGraph::packetStateSeed(
    const GpuSubmissionPacketId& packet
)const noexcept{
    if(!packet.valid() || packet.generation != m_generation || packet.index >= m_packetStateSeeds.size())
        return nullptr;
    return &m_packetStateSeeds[packet.index];
}

bool GpuRecordedGraph::buildPacketInitialStateSeed(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId& packetID,
    const GpuExternalPacketStateSource* const externalStateSources,
    const usize externalStateSourceCount,
    const CommandListResourceStateHandoff*& outInitialStates
){
    outInitialStates = nullptr;
    if(
        !validFor(compiledGraph)
        || !compiledGraph.validPacket(packetID)
        || (externalStateSourceCount != 0u && !externalStateSources)
    )
        return false;

    m_initialStateSeed.reset();
    m_stateSubsetScratch.reset();
    m_stateMergeScratch.reset();
    m_externalBaseStateSeed.reset();
    m_externalMergedStateSeed.reset();

    // State snapshots include inherited state as well as resources a producer changed.  Preserve the established
    // base-plus-branches fan-in rule while deriving every filtered branch from graph declarations: a later source
    // may replace the base, but two independent sources may not leave incompatible final state for one resource.
    const auto appendSourceSubset = [&]{
        if(m_stateSubsetScratch.empty())
            return true;

        if(!m_stateMergeScratch.valid())
            return m_stateMergeScratch.copyFrom(m_stateSubsetScratch);

        const CommandListResourceStateHandoff* const branches[] = { &m_stateSubsetScratch };
        if(!m_initialStateSeed.buildFanIn(m_stateMergeScratch, branches, LengthOf(branches)))
            return false;
        return m_stateMergeScratch.copyFrom(m_initialStateSeed);
    };

    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packetID);
    if(!tasks || packet.taskCount == 0u)
        return false;

    for(usize sourceIndex = 0u; sourceIndex < externalStateSourceCount; ++sourceIndex){
        const GpuExternalPacketStateSource& source = externalStateSources[sourceIndex];
        if(!source.states || !source.states->valid())
            return false;

        m_stateMergeScratch.reset();

        for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
            const GpuTaskGraphTaskView task = graph.taskAt(tasks[taskIndex].index);
            for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
                const GpuTaskResourceUse& use = task.resourceUses[useIndex];
                const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
                m_stateSubsetScratch.reset();

                switch(resource.type){
                case GpuGraphResourceType::Texture:{
                    Texture* const texture = graph.textureForResource(use.resource);
                    if(!texture || !m_stateSubsetScratch.buildTextureRangeSubset(
                        *source.states,
                        texture,
                        use.range.textureSubresources
                    ))
                        return false;
                    break;
                }
                case GpuGraphResourceType::Buffer:{
                    Buffer* const buffer = graph.bufferForResource(use.resource);
                    if(!buffer)
                        return false;
                    Buffer* const buffers[] = { buffer };
                    if(!m_stateSubsetScratch.buildResourceSubset(*source.states, nullptr, 0u, buffers, 1u))
                        return false;
                    break;
                }
                case GpuGraphResourceType::AccelStruct:
                    // Acceleration structures lower through their backing buffers.  Their first graph use still
                    // emits a compiler barrier; typed backing-buffer imports supply a state seed when needed.
                    continue;
                case GpuGraphResourceType::HazardDomain:
                    continue;
                default:
                    return false;
                }

                // A given external producer need not have touched every resource declared by the consumer.  Empty
                // subsets deliberately remain absent instead of manufacturing an initial state; a later source or
                // the compiler's transition supplies the state that actually exists.
                if(!appendSourceSubset())
                    return false;
            }
        }

        if(!m_stateMergeScratch.valid())
            continue;

        if(!m_externalBaseStateSeed.valid()){
            if(
                !m_externalBaseStateSeed.copyFrom(m_stateMergeScratch)
                || !m_externalMergedStateSeed.copyFrom(m_stateMergeScratch)
            )
                return false;
            continue;
        }

        const CommandListResourceStateHandoff* const branches[] = {
            &m_externalMergedStateSeed,
            &m_stateMergeScratch,
        };
        if(!m_initialStateSeed.buildFanIn(m_externalBaseStateSeed, branches, LengthOf(branches)))
            return false;
        if(!m_externalMergedStateSeed.copyFrom(m_initialStateSeed))
            return false;
    }

    m_initialStateSeed.reset();
    if(m_externalMergedStateSeed.valid() && !m_initialStateSeed.copyFrom(m_externalMergedStateSeed))
        return false;
    m_stateMergeScratch.reset();

    const auto appendInitialStateSubset = [&](const bool allowEmpty){
        if(m_stateSubsetScratch.empty())
            return allowEmpty;

        if(!m_initialStateSeed.valid())
            return m_initialStateSeed.copyFrom(m_stateSubsetScratch);

        const CommandListResourceStateHandoff* const branches[] = { &m_stateSubsetScratch };
        if(!m_stateMergeScratch.buildFanIn(m_initialStateSeed, branches, LengthOf(branches)))
            return false;
        return m_initialStateSeed.copyFrom(m_stateMergeScratch);
    };

    for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
        const GpuCompiledTask* const compiledTask = compiledGraph.findTask(tasks[taskIndex]);
        const GpuPacketStateSeed* const stateSeeds = compiledGraph.taskPrologueStateSeeds(tasks[taskIndex]);
        if(!compiledTask || (compiledTask->prologueStateSeedCount != 0u && !stateSeeds))
            return false;

        for(u32 seedIndex = 0u; seedIndex < compiledTask->prologueStateSeedCount; ++seedIndex){
            const GpuPacketStateSeed& seed = stateSeeds[seedIndex];
            const CommandListResourceStateHandoff* const sourceStates = packetStateSeed(seed.sourcePacket);
            if(!sourceStates || !sourceStates->valid())
                return false;

            // An empty subset means the producer thunk never tracked a resource it declared as a state source.  Do
            // not silently fall back to the descriptor's creation state: that would reintroduce the stale-state bug
            // this graph-owned seed is meant to eliminate.
            m_stateSubsetScratch.reset();
            if(Texture* const texture = graph.textureForResource(seed.resource)){
                if(!m_stateSubsetScratch.buildTextureRangeSubset(
                    *sourceStates,
                    texture,
                    seed.range.textureSubresources
                ))
                    return false;
            }
            else if(Buffer* const buffer = graph.bufferForResource(seed.resource)){
                Buffer* const buffers[] = { buffer };
                if(!m_stateSubsetScratch.buildResourceSubset(*sourceStates, nullptr, 0u, buffers, 1u))
                    return false;
            }
            else
                return false;
            if(!appendInitialStateSubset(false))
                return false;
        }
    }

    outInitialStates = m_initialStateSeed.valid() ? &m_initialStateSeed : nullptr;
    return true;
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

    const CommandListResourceStateHandoff* initialStates = nullptr;
    if(!outRecordedGraph.buildPacketInitialStateSeed(
        graph,
        compiledGraph,
        desc.packet,
        desc.externalStateSources,
        desc.externalStateSourceCount,
        initialStates
    ))
        return false;
    if(desc.serialStateSeed && !desc.serialStateSeed->valid())
        return false;

    // Keep one ordered predecessor as the complete packet base.  The graph-built seed still supplies any declared
    // internal or external branch result, so the merge follows the same base-plus-branches contract as ordinary
    // packet seeding without discarding prep state that this packet does not itself use.
    if(desc.serialStateSeed){
        if(initialStates){
            const CommandListResourceStateHandoff* const branches[] = { initialStates };
            if(!outRecordedGraph.m_stateMergeScratch.buildFanIn(
                *desc.serialStateSeed,
                branches,
                LengthOf(branches)
            ))
                return false;
            initialStates = &outRecordedGraph.m_stateMergeScratch;
        }
        else
            initialStates = desc.serialStateSeed;
    }

    CommandListResourceStateHandoff* const packetStateSeed = outRecordedGraph.packetStateSeed(desc.packet);
    if(!packetStateSeed)
        return false;
    packetStateSeed->reset();

    const GpuSubmissionPacket& packet = compiledGraph.packet(desc.packet);
    const GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfo(packet.queue);
    if(!queue || queue->queueClass >= CommandQueue::kCount)
        return false;

    CommandListParameters parameters;
    parameters.setQueueType(queue->queueClass);
    CommandListHandle commandList = m_device.createCommandList(parameters);
    if(!commandList)
        return false;

    commandList->open(initialStates);
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
        const GpuCompiledBarrier* const prologueBarriers = compiledGraph.taskPrologueBarriers(task);
        if(compiledTask->prologueBarrierCount > 0u && !prologueBarriers){
            recorded = false;
            break;
        }
        for(u32 barrierIndex = 0u; recorded && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex)
            recorded = graph.applyCompiledBarrier(prologueBarriers[barrierIndex], *commandList);
        if(recorded)
            commandList->commitBarriers();
        commandList->beginMarker(taskView.markerLabel);
        if(recorded)
            recorded = graph.recordTask(task, *commandList, context);
        commandList->endMarker();
        const GpuCompiledBarrier* const epilogueBarriers = compiledGraph.taskEpilogueBarriers(task);
        if(compiledTask->epilogueBarrierCount > 0u && !epilogueBarriers)
            recorded = false;
        for(u32 barrierIndex = 0u; recorded && barrierIndex < compiledTask->epilogueBarrierCount; ++barrierIndex)
            recorded = graph.applyCompiledBarrier(epilogueBarriers[barrierIndex], *commandList);
        if(recorded)
            commandList->commitBarriers();
    }
    commandList->close(packetStateSeed);
    if(!recorded || !commandList->hasCommandBuffer())
        return false;
    GpuRecordedPacket recordedPacket;
    recordedPacket.packet = desc.packet;
    recordedPacket.commandLists[0u] = commandList.get();
    recordedPacket.commandListCount = 1u;
    recordedPacket.ownedCommandLists[0u] = Move(commandList);
    outRecordedGraph.m_packets.push_back(Move(recordedPacket));
    return true;
}


bool GpuNativePacketRecorder::recordPacketsInCompileOrder(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecordDesc* const recordDescs,
    const usize recordDescCount,
    GpuRecordedGraph& outRecordedGraph,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    if(recordDescCount != compiledGraph.packetCount())
        return false;

    return recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        0u,
        recordDescs,
        recordDescCount,
        outRecordedGraph,
        outFailedPacket
    );
}


bool GpuNativePacketRecorder::recordPacketRangeInCompileOrder(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const usize firstPacketIndex,
    const GpuNativePacketRecordDesc* const recordDescs,
    const usize recordDescCount,
    GpuRecordedGraph& outRecordedGraph,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    if(
        !compiledGraph.validFor(graph)
        || !recordDescs
        || recordDescCount == 0u
        || firstPacketIndex >= compiledGraph.packetCount()
        || recordDescCount > compiledGraph.packetCount() - firstPacketIndex
    )
        return false;

    // The compiler emits packet IDs in stable topological order, so native recording preserves the graph's
    // internal state-seed chain without requiring renderer-side stage ladders. Callers can retain only an
    // intentional late tail outside this range.
    for(usize recordDescIndex = 0u; recordDescIndex < recordDescCount; ++recordDescIndex){
        const usize packetIndex = firstPacketIndex + recordDescIndex;
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
        if(recordDescs[recordDescIndex].packet != packet || !recordPacket(
            graph,
            compiledGraph,
            recordDescs[recordDescIndex],
            outRecordedGraph
        )){
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }
    }
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


bool GpuTaskGraphSubmitter::submitPacketsInCompileOrder(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuTaskGraphExternalCompletionToken* const externalCompletionTokens,
    const usize externalCompletionTokenCount,
    const GpuTaskGraphPacketTimingTicket* const timingTickets,
    const usize timingTicketCount,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    return submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        0u,
        compiledGraph.packetCount(),
        externalCompletionTokens,
        externalCompletionTokenCount,
        timingTickets,
        timingTicketCount,
        transaction,
        scratchArena,
        outFailedPacket,
        nullptr
    );
}


bool GpuTaskGraphSubmitter::submitPacketRangeInCompileOrder(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const usize firstPacketIndex,
    const usize packetCount,
    const GpuTaskGraphExternalCompletionToken* const externalCompletionTokens,
    const usize externalCompletionTokenCount,
    const GpuTaskGraphPacketTimingTicket* const timingTickets,
    const usize timingTicketCount,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket,
    const GpuTaskGraphPacketAcceptedCallback* const acceptedCallback
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    if(
        !compiledGraph.validFor(graph)
        || !recordedGraph.validFor(compiledGraph)
        || !transaction.validFor(compiledGraph)
        || packetCount == 0u
        || firstPacketIndex >= compiledGraph.packetCount()
        || packetCount > compiledGraph.packetCount() - firstPacketIndex
        || (externalCompletionTokenCount != 0u && !externalCompletionTokens)
        || (timingTicketCount != 0u && !timingTickets)
        || (acceptedCallback && !acceptedCallback->invoke)
    )
        return false;

    for(usize tokenIndex = 0u; tokenIndex < externalCompletionTokenCount; ++tokenIndex){
        const GpuTaskGraphExternalCompletionToken& token = externalCompletionTokens[tokenIndex];
        if(!token.completion.valid() || !token.token.valid())
            return false;
        for(usize previousIndex = 0u; previousIndex < tokenIndex; ++previousIndex){
            if(externalCompletionTokens[previousIndex].completion == token.completion)
                return false;
        }
    }
    for(usize ticketIndex = 0u; ticketIndex < timingTicketCount; ++ticketIndex){
        const GpuTaskGraphPacketTimingTicket& ticket = timingTickets[ticketIndex];
        if(
            !ticket.timingTicket
            || !compiledGraph.validPacket(ticket.packet)
            || ticket.packet.index < firstPacketIndex
            || ticket.packet.index >= firstPacketIndex + packetCount
        )
            return false;
        for(usize previousIndex = 0u; previousIndex < ticketIndex; ++previousIndex){
            if(timingTickets[previousIndex].packet == ticket.packet)
                return false;
        }
    }

    // Packet IDs follow the compiler's topological task order. submitPacket resolves each internal producer from
    // transaction state, while every external completion remains a range-wide binding rather than a renderer-side
    // per-stage submission argument.
    for(usize packetIndex = firstPacketIndex; packetIndex < firstPacketIndex + packetCount; ++packetIndex){
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
        GpuTimingSubmissionTicket* timingTicket = nullptr;
        for(usize ticketIndex = 0u; ticketIndex < timingTicketCount; ++ticketIndex){
            if(timingTickets[ticketIndex].packet == packet){
                timingTicket = timingTickets[ticketIndex].timingTicket;
                break;
            }
        }
        if(!submitPacket(
            graph,
            compiledGraph,
            recordedGraph,
            packet,
            externalCompletionTokens,
            externalCompletionTokenCount,
            transaction,
            scratchArena,
            timingTicket
        )){
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }
        const QueueSubmissionToken token = transaction.packetToken(packet);
        if(acceptedCallback && (!token.valid() || !acceptedCallback->invoke(
            acceptedCallback->context,
            packet,
            token
        ))){
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
