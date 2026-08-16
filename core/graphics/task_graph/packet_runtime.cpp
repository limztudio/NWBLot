// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime{


inline constexpr Name s_PacketRecordingFrontierScratchArena("graphics/task_graph/packet_recording_frontier");

#if defined(NWB_DEBUG)
[[nodiscard]] bool HasQueueCapabilities(
    const GpuQueueCapability::Mask available,
    const GpuQueueCapability::Mask required
)noexcept{
    return (static_cast<u8>(available) & static_cast<u8>(required)) == static_cast<u8>(required);
}
#endif


[[nodiscard]] bool ValidateTaskPacketStateBindings(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskPacketStateBinding* const taskStateBindings,
    const usize taskStateBindingCount
){
    if(taskStateBindingCount != 0u && !taskStateBindings)
        return false;

    for(usize bindingIndex = 0u; bindingIndex < taskStateBindingCount; ++bindingIndex){
        const GpuTaskPacketStateBinding& binding = taskStateBindings[bindingIndex];
        if(
            !graph.validTask(binding.task)
            || !compiledGraph.findTask(binding.task)
            || binding.externalStateSourceCount == 0u
            || !binding.externalStateSources
        )
            return false;
        for(usize sourceIndex = 0u; sourceIndex < binding.externalStateSourceCount; ++sourceIndex){
            const CommandListResourceStateHandoff* const states =
                binding.externalStateSources[sourceIndex].states
            ;
            if(!states || !states->valid())
                return false;
        }
    }
    return true;
}

// Ordinary external completions may originate on any current-device queue. A completion paired with an imported
// ownership acquire is narrower: it must prove the exact physical source queue that released the resource, or the
// consumer could wait an unrelated timeline and race the Vulkan acquire.
[[nodiscard]] bool ValidateInitialOwnershipCompletion(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId& packetID,
    const GpuExternalCompletionId& completion,
    const QueueSubmissionToken& token
){
    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packetID);
    if(packet.taskCount != 0u && !tasks)
        return false;

    for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
        const GpuCompiledTask* const compiledTask = compiledGraph.findTask(tasks[taskIndex]);
        const GpuCompiledBarrier* const barriers = compiledGraph.taskPrologueBarriers(tasks[taskIndex]);
        if(!compiledTask || (compiledTask->prologueBarrierCount != 0u && !barriers))
            return false;
        for(u32 barrierIndex = 0u; barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(!barrier.isInitialOwnerHandoff)
                continue;
            if(
                barrier.type != GpuCompiledBarrierType::TextureOwnershipAcquire
                && barrier.type != GpuCompiledBarrierType::BufferOwnershipAcquire
            )
                return false;

            const GpuTaskGraphResourceView resource = graph.resourceAt(barrier.resource.index);
            if(resource.initialOwnerCompletion != completion)
                continue;
            const GpuPhysicalQueueInfo* const sourceQueue = compiledGraph.queueInfo(barrier.sourceQueue);
            if(
                !sourceQueue
                || resource.initialOwnerQueue != barrier.sourceQueue
                || resource.initialOwnerReleaseDestinationQueue != barrier.destinationQueue
                || token.queue != sourceQueue->queueClass
                || !token.matchesPhysicalQueue(barrier.sourceQueue.index, barrier.sourceQueue.deviceGeneration)
            )
                return false;
        }
    }
    return true;
}


} // namespace __hidden_gpu_packet_runtime


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuRecordedGraph::reset(const GpuCompiledGraph& compiledGraph){
    m_packets.clear();
    m_packets.resize(compiledGraph.packetCount());
    m_packetStateSeeds.clear();
    m_packetStateSeeds.reserve(compiledGraph.packetCount());
    m_packetRecordingScratch.clear();
    m_packetRecordingScratch.reserve(compiledGraph.packetCount());
    for(usize packetIndex = 0u; packetIndex < compiledGraph.packetCount(); ++packetIndex){
        m_packets[packetIndex].packet = compiledGraph.packetIdAt(packetIndex);
        m_packetStateSeeds.emplace_back(m_arena);
        m_packetRecordingScratch.emplace_back(m_arena);
    }
    m_serialRecordingScratch.initialStateSeed.reset();
    m_serialRecordingScratch.stateSubsetScratch.reset();
    m_serialRecordingScratch.stateMergeScratch.reset();
    m_serialRecordingScratch.externalBaseStateSeed.reset();
    m_serialRecordingScratch.externalMergedStateSeed.reset();
    m_generation = compiledGraph.generation();
    m_deviceGeneration = compiledGraph.deviceGeneration();
    m_valid = compiledGraph.valid();
}

bool GpuRecordedGraph::validFor(const GpuCompiledGraph& compiledGraph)const noexcept{
    return m_valid
        && compiledGraph.valid()
        && m_generation == compiledGraph.generation()
        && m_deviceGeneration == compiledGraph.deviceGeneration()
        && m_packets.size() == compiledGraph.packetCount()
        && m_packetStateSeeds.size() == compiledGraph.packetCount()
        && m_packetRecordingScratch.size() == compiledGraph.packetCount()
    ;
}

bool GpuTaskGraphExternalCompletionToken::validFor(const GpuCompiledGraph& compiledGraph)const noexcept{
    if(
        !compiledGraph.valid()
        || !completion.valid()
        || completion.generation != compiledGraph.generation()
        || !token.valid()
        || !token.hasPhysicalQueueIdentity()
    )
        return false;

    // The producer can be absent from this frame's topology (for example after an async lane is disabled), so the
    // graph validates device lifetime here and leaves concrete queue validation to the submitting Device.
    return token.deviceGeneration == compiledGraph.deviceGeneration();
}

const GpuRecordedPacket* GpuRecordedGraph::find(const GpuSubmissionPacketId& packet)const noexcept{
    if(
        !packet.valid()
        || packet.generation != m_generation
        || packet.index >= m_packets.size()
    )
        return nullptr;
    const GpuRecordedPacket& recordedPacket = m_packets[packet.index];
    return recordedPacket.packet == packet && recordedPacket.commandListCount != 0u
        ? &recordedPacket
        : nullptr
    ;
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

GpuRecordedGraph::PacketRecordingScratch* GpuRecordedGraph::packetRecordingScratch(
    const GpuSubmissionPacketId& packet
)noexcept{
    if(!packet.valid() || packet.generation != m_generation || packet.index >= m_packetRecordingScratch.size())
        return nullptr;
    return &m_packetRecordingScratch[packet.index];
}

bool GpuRecordedGraph::buildPacketInitialStateSeed(
    PacketRecordingScratch& scratch,
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId& packetID,
    const GpuExternalPacketStateSource* const externalStateSources,
    const usize externalStateSourceCount,
    const GpuTaskPacketStateBinding* const taskStateBindings,
    const usize taskStateBindingCount,
    const CommandListResourceStateHandoff*& outInitialStates
){
    outInitialStates = nullptr;
    if(
        !validFor(compiledGraph)
        || !compiledGraph.validPacket(packetID)
        || (externalStateSourceCount != 0u && !externalStateSources)
        || !__hidden_gpu_packet_runtime::ValidateTaskPacketStateBindings(
            graph,
            compiledGraph,
            taskStateBindings,
            taskStateBindingCount
        )
    )
        return false;

    scratch.initialStateSeed.reset();
    scratch.stateSubsetScratch.reset();
    scratch.stateMergeScratch.reset();
    scratch.externalBaseStateSeed.reset();
    scratch.externalMergedStateSeed.reset();

    // State snapshots include inherited state as well as resources a producer changed.  Preserve the established
    // base-plus-branches fan-in rule while deriving every filtered branch from graph declarations: a later source
    // may replace the base, but two independent sources may not leave incompatible final state for one resource.
    const auto appendSourceSubset = [&]{
        if(scratch.stateSubsetScratch.empty())
            return true;

        if(!scratch.stateMergeScratch.valid())
            return scratch.stateMergeScratch.copyFrom(scratch.stateSubsetScratch);

        const CommandListResourceStateHandoff* const branches[] = { &scratch.stateSubsetScratch };
        if(!scratch.initialStateSeed.buildFanIn(scratch.stateMergeScratch, branches, LengthOf(branches)))
            return false;
        return scratch.stateMergeScratch.copyFrom(scratch.initialStateSeed);
    };

    const auto appendMergedExternalSource = [&]{
        if(!scratch.stateMergeScratch.valid())
            return true;

        if(!scratch.externalBaseStateSeed.valid()){
            if(
                !scratch.externalBaseStateSeed.copyFrom(scratch.stateMergeScratch)
                || !scratch.externalMergedStateSeed.copyFrom(scratch.stateMergeScratch)
            )
                return false;
            return true;
        }

        const CommandListResourceStateHandoff* const branches[] = {
            &scratch.externalMergedStateSeed,
            &scratch.stateMergeScratch,
        };
        if(!scratch.initialStateSeed.buildFanIn(scratch.externalBaseStateSeed, branches, LengthOf(branches)))
            return false;
        return scratch.externalMergedStateSeed.copyFrom(scratch.initialStateSeed);
    };

    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packetID);
    if(!tasks || packet.taskCount == 0u)
        return false;

    const auto appendStateSource = [&](
        const CommandListResourceStateHandoff* const sourceStates,
        const GpuTaskId* const sourceTasks,
        const u32 sourceTaskCount
    ){
        if(
            !sourceStates
            || !sourceStates->valid()
            || (sourceTaskCount != 0u && !sourceTasks)
        )
            return false;

        scratch.stateMergeScratch.reset();

        for(u32 taskIndex = 0u; taskIndex < sourceTaskCount; ++taskIndex){
            const GpuTaskGraphTaskView task = graph.taskAt(sourceTasks[taskIndex].index);
            for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
                const GpuTaskResourceUse& use = task.resourceUses[useIndex];
                const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
                scratch.stateSubsetScratch.reset();

                switch(resource.type){
                case GpuGraphResourceType::Texture:{
                    Texture* const texture = graph.textureForResource(use.resource);
                    if(!texture || !scratch.stateSubsetScratch.buildTextureRangeSubset(
                        *sourceStates,
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
                    if(!scratch.stateSubsetScratch.buildResourceSubset(*sourceStates, nullptr, 0u, buffers, 1u))
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

        return appendMergedExternalSource();
    };

    const auto appendInitialOwnerStateSource = [&](const GpuCompiledBarrier& barrier){
        const GpuTaskGraphResourceView resource = graph.resourceAt(barrier.resource.index);
        const CommandListResourceStateHandoff* const sourceStates = resource.initialOwnerStateSource;
        if(
            !sourceStates
            || !sourceStates->valid()
            || resource.initialOwnerQueue != barrier.sourceQueue
            || resource.initialOwnerReleaseDestinationQueue != barrier.destinationQueue
        )
            return false;

        scratch.stateMergeScratch.reset();
        scratch.stateSubsetScratch.reset();
        switch(resource.type){
        case GpuGraphResourceType::Texture:{
            Texture* const texture = graph.textureForResource(barrier.resource);
            if(!texture || !scratch.stateSubsetScratch.buildTextureRangeSubset(
                *sourceStates,
                texture,
                barrier.range.textureSubresources
            ))
                return false;
            break;
        }
        case GpuGraphResourceType::Buffer:{
            Buffer* const buffer = graph.bufferForResource(barrier.resource);
            if(!buffer)
                return false;
            Buffer* const buffers[] = { buffer };
            if(!scratch.stateSubsetScratch.buildResourceSubset(*sourceStates, nullptr, 0u, buffers, 1u))
                return false;
            break;
        }
        default:
            return false;
        }

        // Unlike an optional task state source, an imported ownership handoff must name every first-use range. A
        // missing state would leave the acquire marker without the native source layout/owner it is required to
        // import, so reject recording instead of falling back to descriptor creation state.
        if(scratch.stateSubsetScratch.empty() || !appendSourceSubset())
            return false;
        return appendMergedExternalSource();
    };

    // Resource-owned external handoffs are resolved from compiler markers rather than a renderer packet override.
    // They are imported before ordinary task sources so the native command list opens with the released ownership
    // and emits the paired Vulkan acquire before any graph prologue transition records.
    for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
        const GpuCompiledTask* const compiledTask = compiledGraph.findTask(tasks[taskIndex]);
        const GpuCompiledBarrier* const barriers = compiledGraph.taskPrologueBarriers(tasks[taskIndex]);
        if(!compiledTask || (compiledTask->prologueBarrierCount != 0u && !barriers))
            return false;
        for(u32 barrierIndex = 0u; barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                !barrier.isInitialOwnerHandoff
                || (
                    barrier.type != GpuCompiledBarrierType::TextureOwnershipAcquire
                    && barrier.type != GpuCompiledBarrierType::BufferOwnershipAcquire
                )
            )
                continue;
            const GpuTaskGraphResourceView resource = graph.resourceAt(barrier.resource.index);
            if(!resource.initialOwnerCompletion.valid())
                continue;
            if(!appendInitialOwnerStateSource(barrier))
                return false;
        }
    }

    // Declaration-owned sources are attached to the task that needs their imported state.  That preserves the
    // source/resource relationship across packet coalescing and lets ordinary record traversal avoid renderer-owned
    // packet-specific overrides.  Sparse record-descriptor sources below remain as a compatibility bridge.
    for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(tasks[taskIndex].index);
        if(task.externalStateSourceCount != 0u && !task.externalStateSources)
            return false;
        for(usize sourceIndex = 0u; sourceIndex < task.externalStateSourceCount; ++sourceIndex){
            if(!appendStateSource(
                task.externalStateSources[sourceIndex].states,
                tasks + taskIndex,
                1u
            ))
                return false;
        }
    }

    // Late sources remain a recording-time bridge, but their identity is graph-owned: the caller names the task
    // that semantically receives the imported state and the recorder resolves its current compiled packet.  Apply
    // each source to the complete packet to preserve the established packet-wide external handoff while compiler
    // packetization evolves.
    for(usize bindingIndex = 0u; bindingIndex < taskStateBindingCount; ++bindingIndex){
        const GpuTaskPacketStateBinding& binding = taskStateBindings[bindingIndex];
        if(compiledGraph.packetForTask(binding.task) != packetID)
            continue;
        for(usize sourceIndex = 0u; sourceIndex < binding.externalStateSourceCount; ++sourceIndex){
            if(!appendStateSource(
                binding.externalStateSources[sourceIndex].states,
                tasks,
                packet.taskCount
            ))
                return false;
        }
    }

    for(usize sourceIndex = 0u; sourceIndex < externalStateSourceCount; ++sourceIndex){
        if(!appendStateSource(
            externalStateSources[sourceIndex].states,
            tasks,
            packet.taskCount
        ))
            return false;
    }

    scratch.initialStateSeed.reset();
    if(
        scratch.externalMergedStateSeed.valid()
        && !scratch.initialStateSeed.copyFrom(scratch.externalMergedStateSeed)
    )
        return false;
    scratch.stateMergeScratch.reset();

    const auto appendInitialStateSubset = [&](const bool allowEmpty){
        if(scratch.stateSubsetScratch.empty())
            return allowEmpty;

        if(!scratch.initialStateSeed.valid())
            return scratch.initialStateSeed.copyFrom(scratch.stateSubsetScratch);

        const CommandListResourceStateHandoff* const branches[] = { &scratch.stateSubsetScratch };
        if(!scratch.stateMergeScratch.buildFanIn(scratch.initialStateSeed, branches, LengthOf(branches)))
            return false;
        return scratch.initialStateSeed.copyFrom(scratch.stateMergeScratch);
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
            scratch.stateSubsetScratch.reset();
            if(Texture* const texture = graph.textureForResource(seed.resource)){
                if(!scratch.stateSubsetScratch.buildTextureRangeSubset(
                    *sourceStates,
                    texture,
                    seed.range.textureSubresources
                ))
                    return false;
            }
            else if(Buffer* const buffer = graph.bufferForResource(seed.resource)){
                Buffer* const buffers[] = { buffer };
                if(!scratch.stateSubsetScratch.buildResourceSubset(*sourceStates, nullptr, 0u, buffers, 1u))
                    return false;
            }
            else
                return false;
            if(!appendInitialStateSubset(false))
                return false;
        }
    }

    outInitialStates = scratch.initialStateSeed.valid() ? &scratch.initialStateSeed : nullptr;
    return true;
}


bool GpuNativePacketRecorder::recordPacket(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecordDesc& desc,
    GpuRecordedGraph& outRecordedGraph,
    GpuCommandIrCapture* const commandIrCapture,
    const GpuTaskPacketStateBinding* const taskStateBindings,
    const usize taskStateBindingCount
)const{
    if(
        !compiledGraph.validFor(graph)
        || !compiledGraph.validPacket(desc.packet)
        || !__hidden_gpu_packet_runtime::ValidateTaskPacketStateBindings(
            graph,
            compiledGraph,
            taskStateBindings,
            taskStateBindingCount
        )
    )
        return false;
    if(!outRecordedGraph.validFor(compiledGraph))
        outRecordedGraph.reset(compiledGraph);
    return recordPacketWithScratch(
        graph,
        compiledGraph,
        desc,
        outRecordedGraph,
        outRecordedGraph.m_serialRecordingScratch,
        commandIrCapture,
        taskStateBindings,
        taskStateBindingCount
    );
}


bool GpuNativePacketRecorder::recordPacketWithScratch(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecordDesc& desc,
    GpuRecordedGraph& outRecordedGraph,
    GpuRecordedGraph::PacketRecordingScratch& scratch,
    GpuCommandIrCapture* const commandIrCapture,
    const GpuTaskPacketStateBinding* const taskStateBindings,
    const usize taskStateBindingCount
)const{
    if(
        !compiledGraph.validFor(graph)
        || !compiledGraph.validPacket(desc.packet)
        || !outRecordedGraph.validFor(compiledGraph)
        || !__hidden_gpu_packet_runtime::ValidateTaskPacketStateBindings(
            graph,
            compiledGraph,
            taskStateBindings,
            taskStateBindingCount
        )
    )
        return false;
    // A capture is one graph-generation artifact. Reject a stale non-empty capture before opening a packet that
    // happens not to contain a primitive command; otherwise old records could be mistaken for this packet's trace.
    if(
        commandIrCapture
        && commandIrCapture->recordCount() != 0u
        && commandIrCapture->graphGeneration() != compiledGraph.generation()
    )
        return false;
    if(outRecordedGraph.find(desc.packet))
        return false;

    const CommandListResourceStateHandoff* initialStates = nullptr;
    if(!outRecordedGraph.buildPacketInitialStateSeed(
        scratch,
        graph,
        compiledGraph,
        desc.packet,
        desc.externalStateSources,
        desc.externalStateSourceCount,
        taskStateBindings,
        taskStateBindingCount,
        initialStates
    ))
        return false;

    CommandListResourceStateHandoff* const packetStateSeed = outRecordedGraph.packetStateSeed(desc.packet);
    if(!packetStateSeed)
        return false;
    packetStateSeed->reset();

    const GpuSubmissionPacket& packet = compiledGraph.packet(desc.packet);
    const GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfo(packet.queue);
    if(
        !queue
        || queue->queueClass >= CommandQueue::kCount
        || !m_device.matchesPhysicalQueueIdentity(packet.queue)
    )
        return false;

    const usize captureRecordCount = commandIrCapture ? commandIrCapture->recordCount() : 0u;

    CommandListParameters parameters;
    parameters.setPhysicalQueue(packet.queue);
    parameters.setRecordingWorkerIndex(desc.recordingWorkerIndex);
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
            .taskGraph = graph,
            .graph = compiledGraph,
            .task = task,
            .packet = desc.packet,
            .queue = packet.queue,
            .commandIrCapture = commandIrCapture,
        };
        const GpuCompiledBarrier* const prologueBarriers = compiledGraph.taskPrologueBarriers(task);
        if(compiledTask->prologueBarrierCount > 0u && !prologueBarriers){
            recorded = false;
            break;
        }
        for(u32 barrierIndex = 0u; recorded && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex)
            recorded = graph.applyCompiledBarrier(compiledGraph, prologueBarriers[barrierIndex], *commandList);
        // A retained state that already matches the compiler plan still needs a native tracker entry. Otherwise a
        // later packet cannot import that graph-declared buffer state, and a renderer thunk would need a redundant
        // direct transition merely to publish its handoff.
        if(recorded)
            recorded = graph.seedTaskRetainedBufferStates(task, *commandList);
        if(recorded)
            commandList->commitBarriers();
        commandList->beginMarker(taskView.markerLabel);
#if defined(NWB_DEBUG)
        commandList->beginTaskCapabilityTracking();
#endif
        if(recorded)
            recorded = graph.recordTask(task, *commandList, context);
#if defined(NWB_DEBUG)
        const GpuQueueCapability::Mask usedCapabilities = commandList->endTaskCapabilityTracking();
        if(
            recorded
            && (
                !__hidden_gpu_packet_runtime::HasQueueCapabilities(
                    taskView.queue.requiredCapabilities,
                    usedCapabilities
                )
                || !__hidden_gpu_packet_runtime::HasQueueCapabilities(queue->capabilities, usedCapabilities)
            )
        ){
            NWB_LOGGER_CRITICAL_WARNING(
                NWB_TEXT("Gpu task graph: rejecting task '{}' because capability mask {} is outside declared mask {} on assigned queue {}:{} (mask {})"),
                StringConvert(taskView.markerLabel),
                static_cast<u32>(usedCapabilities),
                static_cast<u32>(taskView.queue.requiredCapabilities),
                static_cast<u32>(queue->queueClass),
                queue->id.index,
                static_cast<u32>(queue->capabilities)
            );
            recorded = false;
        }
#endif
        commandList->endMarker();
        const GpuCompiledBarrier* const epilogueBarriers = compiledGraph.taskEpilogueBarriers(task);
        if(compiledTask->epilogueBarrierCount > 0u && !epilogueBarriers)
            recorded = false;
        for(u32 barrierIndex = 0u; recorded && barrierIndex < compiledTask->epilogueBarrierCount; ++barrierIndex)
            recorded = graph.applyCompiledBarrier(compiledGraph, epilogueBarriers[barrierIndex], *commandList);
        if(recorded)
            commandList->commitBarriers();
    }
    commandList->close(packetStateSeed);
    if(!recorded || !commandList->hasCommandBuffer()){
        if(commandIrCapture)
            commandIrCapture->rollback(captureRecordCount);
        return false;
    }
    GpuRecordedPacket& recordedPacket = outRecordedGraph.m_packets[desc.packet.index];
    recordedPacket.packet = desc.packet;
    recordedPacket.commandLists[0u] = commandList.get();
    recordedPacket.ownedCommandLists[0u] = Move(commandList);
    // Publish the slot only after its owned native list is retained. Frontier workers are joined before callers can
    // submit, but this order also keeps the slot self-consistent for diagnostic reads.
    recordedPacket.commandListCount = 1u;
    recordedPacket.recordingWorkerIndex = desc.recordingWorkerIndex;
    return true;
}


bool GpuNativePacketRecorder::recordPacketRangeInCompileOrder(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketRange& range,
    const GpuNativePacketRecordDesc* const recordOverrides,
    const usize recordOverrideCount,
    GpuRecordedGraph& outRecordedGraph,
    GpuSubmissionPacketId* const outFailedPacket,
    GpuCommandIrCapture* const commandIrCapture,
    const GpuTaskPacketStateBinding* const taskStateBindings,
    const usize taskStateBindingCount
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    if(
        !compiledGraph.validFor(graph)
        || !compiledGraph.validPacketRange(range)
        || (recordOverrideCount != 0u && !recordOverrides)
        || !__hidden_gpu_packet_runtime::ValidateTaskPacketStateBindings(
            graph,
            compiledGraph,
            taskStateBindings,
            taskStateBindingCount
        )
    )
        return false;

    const usize rangeBegin = range.first.index;
    const usize rangeEnd = rangeBegin + range.packetCount;
    for(usize overrideIndex = 0u; overrideIndex < recordOverrideCount; ++overrideIndex){
        const GpuNativePacketRecordDesc& overrideDesc = recordOverrides[overrideIndex];
        if(
            !compiledGraph.validPacket(overrideDesc.packet)
            || overrideDesc.packet.index < rangeBegin
            || overrideDesc.packet.index >= rangeEnd
        )
            return false;
        for(usize previousOverrideIndex = 0u; previousOverrideIndex < overrideIndex; ++previousOverrideIndex){
            if(recordOverrides[previousOverrideIndex].packet == overrideDesc.packet)
                return false;
        }
    }

    // The compiler emits packet IDs in stable topological order, so native recording preserves the graph's
    // internal state-seed chain without requiring renderer-side packet collectors. Callers supply only sparse
    // external-state overrides and can retain an intentional late tail outside this range.
    for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
        GpuNativePacketRecordDesc defaultDesc{
            .packet = packet,
        };
        const GpuNativePacketRecordDesc* recordDesc = &defaultDesc;
        for(usize overrideIndex = 0u; overrideIndex < recordOverrideCount; ++overrideIndex){
            if(recordOverrides[overrideIndex].packet == packet){
                recordDesc = &recordOverrides[overrideIndex];
                break;
            }
        }
        if(!recordPacket(
            graph,
            compiledGraph,
            *recordDesc,
            outRecordedGraph,
            commandIrCapture,
            taskStateBindings,
            taskStateBindingCount
        )){
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }
    }
    return true;
}


bool GpuNativePacketRecorder::recordPacketRangeInReadyFrontiers(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketRange& range,
    const GpuNativePacketRecordDesc* const recordOverrides,
    const usize recordOverrideCount,
    GpuRecordedGraph& outRecordedGraph,
    Alloc::ThreadPool& workerPool,
    GpuSubmissionPacketId* const outFailedPacket,
    GpuCommandIrCapture* const commandIrCapture,
    const GpuTaskPacketStateBinding* const taskStateBindings,
    const usize taskStateBindingCount
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    if(
        !compiledGraph.validFor(graph)
        || !compiledGraph.validPacketRange(range)
        || (recordOverrideCount != 0u && !recordOverrides)
        || !__hidden_gpu_packet_runtime::ValidateTaskPacketStateBindings(
            graph,
            compiledGraph,
            taskStateBindings,
            taskStateBindingCount
        )
    )
        return false;

    // Command-IR records form one linear graph-generation artifact. Keeping capture serial preserves its existing
    // record order and rollback contract; native packets remain eligible for worker recording when capture is off.
    if(
        commandIrCapture
        || !workerPool.isParallelEnabled()
        || range.packetCount < 2u
    ){
        return recordPacketRangeInCompileOrder(
            graph,
            compiledGraph,
            range,
            recordOverrides,
            recordOverrideCount,
            outRecordedGraph,
            outFailedPacket,
            commandIrCapture,
            taskStateBindings,
            taskStateBindingCount
        );
    }

    const usize rangeBegin = range.first.index;
    const usize rangeEnd = rangeBegin + range.packetCount;
    for(usize overrideIndex = 0u; overrideIndex < recordOverrideCount; ++overrideIndex){
        const GpuNativePacketRecordDesc& overrideDesc = recordOverrides[overrideIndex];
        if(
            !compiledGraph.validPacket(overrideDesc.packet)
            || overrideDesc.packet.index < rangeBegin
            || overrideDesc.packet.index >= rangeEnd
        )
            return false;
        for(usize previousOverrideIndex = 0u; previousOverrideIndex < overrideIndex; ++previousOverrideIndex){
            if(recordOverrides[previousOverrideIndex].packet == overrideDesc.packet)
                return false;
        }
    }

    if(!outRecordedGraph.validFor(compiledGraph))
        outRecordedGraph.reset(compiledGraph);

    Alloc::ScratchArena scratchArena(__hidden_gpu_packet_runtime::s_PacketRecordingFrontierScratchArena);
    Vector<GpuNativePacketRecordDesc, Alloc::ScratchArena> recordDescs(scratchArena);
    recordDescs.reserve(range.packetCount);
    u32 maximumFrontier = 0u;
    for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
        GpuNativePacketRecordDesc desc{
            .packet = packet,
        };
        for(usize overrideIndex = 0u; overrideIndex < recordOverrideCount; ++overrideIndex){
            if(recordOverrides[overrideIndex].packet == packet){
                desc = recordOverrides[overrideIndex];
                break;
            }
        }
        recordDescs.push_back(desc);
        const u32 frontier = compiledGraph.packet(packet).recordingFrontier;
        if(frontier > maximumFrontier)
            maximumFrontier = frontier;
    }

    const auto packetDependenciesAreRecorded = [&](const GpuSubmissionPacketId packet){
        const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
        const GpuPacketDependency* const dependencies = compiledGraph.packetDependencies(packet);
        if(packetPlan.dependencyCount != 0u && !dependencies)
            return false;
        for(u32 dependencyIndex = 0u; dependencyIndex < packetPlan.dependencyCount; ++dependencyIndex){
            if(!outRecordedGraph.find(dependencies[dependencyIndex].producer))
                return false;
        }
        return true;
    };
    const auto packetAllowsParallelRecording = [&](const GpuNativePacketRecordDesc& desc){
        // Raw record-descriptor sources and late task bindings still bridge work outside the compiler's internal
        // packet edges. Preserve serial recording for those runtime inputs. Task-declared sources are immutable
        // graph-owned snapshots, so an explicitly opted-in task may safely consume them on a worker frontier.
        if(desc.externalStateSourceCount != 0u)
            return false;

        const GpuSubmissionPacketId packet = desc.packet;
        for(usize bindingIndex = 0u; bindingIndex < taskStateBindingCount; ++bindingIndex){
            if(compiledGraph.packetForTask(taskStateBindings[bindingIndex].task) == packet)
                return false;
        }
        const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
        const GpuTaskId* const tasks = compiledGraph.packetTasks(packet);
        if(!tasks || packetPlan.taskCount == 0u)
            return false;
        for(u32 taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskGraphTaskView task = graph.taskAt(tasks[taskIndex].index);
            if(!task.scheduling.allowParallelRecording)
                return false;
        }
        return true;
    };

    Vector<usize, Alloc::ScratchArena> parallelDescIndices(scratchArena);
    Vector<u8, Alloc::ScratchArena> parallelResults(scratchArena);
    for(u32 frontier = 0u;; ++frontier){
        parallelDescIndices.clear();
        for(usize descIndex = 0u; descIndex < recordDescs.size(); ++descIndex){
            const GpuNativePacketRecordDesc& desc = recordDescs[descIndex];
            if(compiledGraph.packet(desc.packet).recordingFrontier != frontier)
                continue;
            if(!packetDependenciesAreRecorded(desc.packet)){
                if(outFailedPacket)
                    *outFailedPacket = desc.packet;
                return false;
            }
            if(packetAllowsParallelRecording(desc))
                parallelDescIndices.push_back(descIndex);
            else if(!recordPacketWithScratch(
                graph,
                compiledGraph,
                desc,
                outRecordedGraph,
                outRecordedGraph.m_serialRecordingScratch,
                nullptr,
                taskStateBindings,
                taskStateBindingCount
            )){
                if(outFailedPacket)
                    *outFailedPacket = desc.packet;
                return false;
            }
        }

        if(!parallelDescIndices.empty()){
            parallelResults.resize(parallelDescIndices.size());
            workerPool.parallelFor(0u, parallelDescIndices.size(), [&](const usize parallelIndex){
                const GpuNativePacketRecordDesc& desc = recordDescs[parallelDescIndices[parallelIndex]];
                GpuRecordedGraph::PacketRecordingScratch* const scratch = outRecordedGraph.packetRecordingScratch(desc.packet);
                GpuNativePacketRecordDesc workerDesc = desc;
                // Reserve zero for serial/direct command lists. ThreadPool's caller is worker zero, so shift every
                // ready-frontier lease by one and retain a stable distinction even when the caller records a chunk.
                workerDesc.recordingWorkerIndex = static_cast<u32>(workerPool.currentWorkerIndex() + 1u);
                parallelResults[parallelIndex] = scratch && recordPacketWithScratch(
                    graph,
                    compiledGraph,
                    workerDesc,
                    outRecordedGraph,
                    *scratch,
                    nullptr,
                    taskStateBindings,
                    taskStateBindingCount
                ) ? 1u : 0u;
            });
            for(usize parallelIndex = 0u; parallelIndex < parallelResults.size(); ++parallelIndex){
                if(parallelResults[parallelIndex] != 0u)
                    continue;
                if(outFailedPacket)
                    *outFailedPacket = recordDescs[parallelDescIndices[parallelIndex]].packet;
                return false;
            }
        }
        if(frontier == maximumFrontier)
            break;
    }
    return true;
}


void GpuGraphSubmissionTransaction::reset(const GpuCompiledGraph& compiledGraph){
    m_packets.clear();
    m_latestAcceptedQueueTokens.clear();
    m_generation = compiledGraph.generation();
    m_deviceGeneration = compiledGraph.deviceGeneration();
    m_acceptedSubmissionCount = 0u;
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

bool GpuGraphSubmissionTransaction::acceptPacket(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId& packetID,
    const QueueSubmissionToken& token
)noexcept{
    if(!validFor(compiledGraph) || !compiledGraph.validPacket(packetID) || !token.valid())
        return false;
    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    const GpuPhysicalQueueInfo* const queueInfo = compiledGraph.queueInfo(packet.queue);
    if(
        !queueInfo
        || queueInfo->queueClass >= CommandQueue::kCount
        || token.queue != queueInfo->queueClass
        || !token.matchesPhysicalQueue(packet.queue.index, packet.queue.deviceGeneration)
    )
        return false;
    GpuPacketRuntime& runtime = m_packets[packetID.index];
    if(runtime.state != GpuPacketRuntimeState::Recorded)
        return false;

    runtime.state = GpuPacketRuntimeState::Accepted;
    runtime.token = token;
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packetID);
    for(u32 taskIndex = 0u; tasks && taskIndex < packet.taskCount; ++taskIndex)
        graph.acceptTask(tasks[taskIndex], token);

    ++m_acceptedSubmissionCount;
    if(m_acceptedSubmissionCount == 0u)
        ++m_acceptedSubmissionCount;

    for(LatestAcceptedQueueToken& latest : m_latestAcceptedQueueTokens){
        if(latest.queue == packet.queue){
            latest.token = token;
            return true;
        }
    }
    m_latestAcceptedQueueTokens.push_back(LatestAcceptedQueueToken{
        .queue = packet.queue,
        .token = token,
    });
    return true;
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

bool GpuGraphSubmissionTransaction::appendAcceptedQueueFrontierWaitTokens(
    const GpuPhysicalQueueId& destinationQueue,
    Vector<QueueSubmissionToken, Alloc::ScratchArena>& outTokens
)const{
    if(!m_valid || !destinationQueue.valid() || destinationQueue.deviceGeneration != m_deviceGeneration)
        return false;

    for(const LatestAcceptedQueueToken& latest : m_latestAcceptedQueueTokens){
        if(latest.queue == destinationQueue)
            continue;
        if(!latest.queue.valid() || latest.queue.deviceGeneration != m_deviceGeneration || !latest.token.valid())
            return false;
        // The transaction holds one newest accepted token per physical queue, so this cannot duplicate a producer
        // even when a queue accepted several packets before the recovery tail is armed.
        outTokens.push_back(latest.token);
    }
    return true;
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
    GpuTimingSubmissionTicket* const timingTicket,
    const QueueSubmissionPreSubmitHook* const preSubmitHook
)const{
    if(
        !compiledGraph.validFor(graph)
        || !compiledGraph.validPacket(packetID)
        || !recordedGraph.validFor(compiledGraph)
        || !transaction.validFor(compiledGraph)
        || (externalCompletionTokenCount > 0u && !externalCompletionTokens)
        || (preSubmitHook && !preSubmitHook->valid())
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
        || !m_device.matchesPhysicalQueueIdentity(packet.queue)
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
    waitTokens.reserve(
        packet.dependencyCount
        + packet.externalDependencyCount
        + (packet.joinsAcceptedQueueFrontier ? compiledGraph.packetCount() : 0u)
    );
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
                if(
                    !binding.validFor(compiledGraph)
                    || !__hidden_gpu_packet_runtime::ValidateInitialOwnershipCompletion(
                        graph,
                        compiledGraph,
                        packetID,
                        completion,
                        binding.token
                    )
                ){
                    transaction.rejectPacket(graph, compiledGraph, packetID);
                    return false;
                }
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

    if(
        packet.joinsAcceptedQueueFrontier
        && !transaction.appendAcceptedQueueFrontierWaitTokens(packet.queue, waitTokens)
    ){
        transaction.rejectPacket(graph, compiledGraph, packetID);
        return false;
    }

    QueueSubmissionDesc submitDesc;
    if(!waitTokens.empty())
        submitDesc.setWaitTokens(waitTokens.data(), waitTokens.size());
    if(preSubmitHook)
        submitDesc.setPreSubmitHook(*preSubmitHook);
    const QueueSubmissionToken token = timingTicket
        ? timingTicket->submit(
            m_device,
            recordedPacket->commandLists,
            recordedPacket->commandListCount,
            packet.queue,
            submitDesc
        )
        : m_device.executeCommandLists(
            recordedPacket->commandLists,
            recordedPacket->commandListCount,
            packet.queue,
            submitDesc
        )
    ;
    if(!token.valid()){
        transaction.rejectPacket(graph, compiledGraph, packetID);
        return false;
    }

    NWB_ASSERT(token.matchesPhysicalQueue(packet.queue.index, packet.queue.deviceGeneration));
    return transaction.acceptPacket(graph, compiledGraph, packetID, token);
}


bool GpuTaskGraphSubmitter::submitPacketRangeInCompileOrder(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuSubmissionPacketRange& range,
    const GpuTaskGraphExternalCompletionToken* const externalCompletionTokens,
    const usize externalCompletionTokenCount,
    const GpuTaskGraphPacketTimingTicket* const timingTickets,
    const usize timingTicketCount,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket,
    const GpuTaskGraphPacketAcceptedCallback* const acceptedCallback,
    const GpuTaskGraphPacketSubmissionHook* const submissionHooks,
    const usize submissionHookCount
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    if(
        !compiledGraph.validFor(graph)
        || !recordedGraph.validFor(compiledGraph)
        || !transaction.validFor(compiledGraph)
        || !compiledGraph.validPacketRange(range)
        || (externalCompletionTokenCount != 0u && !externalCompletionTokens)
        || (timingTicketCount != 0u && !timingTickets)
        || (submissionHookCount != 0u && !submissionHooks)
        || (acceptedCallback && !acceptedCallback->invoke)
    )
        return false;

    for(usize tokenIndex = 0u; tokenIndex < externalCompletionTokenCount; ++tokenIndex){
        const GpuTaskGraphExternalCompletionToken& token = externalCompletionTokens[tokenIndex];
        if(!token.validFor(compiledGraph))
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
            || ticket.packet.index < range.first.index
            || static_cast<usize>(ticket.packet.index) >= static_cast<usize>(range.first.index) + range.packetCount
        )
            return false;
        for(usize previousIndex = 0u; previousIndex < ticketIndex; ++previousIndex){
            if(timingTickets[previousIndex].packet == ticket.packet)
                return false;
        }
    }
    for(usize hookIndex = 0u; hookIndex < submissionHookCount; ++hookIndex){
        const GpuTaskGraphPacketSubmissionHook& hook = submissionHooks[hookIndex];
        if(
            !hook.hook.valid()
            || !compiledGraph.validPacket(hook.packet)
            || hook.packet.index < range.first.index
            || static_cast<usize>(hook.packet.index) >= static_cast<usize>(range.first.index) + range.packetCount
        )
            return false;
        for(usize previousIndex = 0u; previousIndex < hookIndex; ++previousIndex){
            if(submissionHooks[previousIndex].packet == hook.packet)
                return false;
        }
    }

    // Packet IDs follow the compiler's topological task order. submitPacket resolves each internal producer from
    // transaction state, while every external completion remains a range-wide binding rather than a renderer-side
    // per-stage submission argument.
    for(
        usize packetIndex = range.first.index;
        packetIndex < static_cast<usize>(range.first.index) + range.packetCount;
        ++packetIndex
    ){
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
        GpuTimingSubmissionTicket* timingTicket = nullptr;
        for(usize ticketIndex = 0u; ticketIndex < timingTicketCount; ++ticketIndex){
            if(timingTickets[ticketIndex].packet == packet){
                timingTicket = timingTickets[ticketIndex].timingTicket;
                break;
            }
        }
        const QueueSubmissionPreSubmitHook* preSubmitHook = nullptr;
        for(usize hookIndex = 0u; hookIndex < submissionHookCount; ++hookIndex){
            if(submissionHooks[hookIndex].packet == packet){
                preSubmitHook = &submissionHooks[hookIndex].hook;
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
            timingTicket,
            preSubmitHook
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


bool GpuTaskGraphSubmitter::submitPacketRangeInCompileOrderFromTasks(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuSubmissionPacketRange& range,
    const GpuTaskGraphExternalCompletionToken* const externalCompletionTokens,
    const usize externalCompletionTokenCount,
    const GpuTaskGraphTaskTimingTicket* const taskTimingTickets,
    const usize taskTimingTicketCount,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket,
    const GpuTaskGraphPacketAcceptedCallback* const acceptedCallback,
    const GpuTaskGraphPacketSubmissionHook* const submissionHooks,
    const usize submissionHookCount
)const{
    if(
        !compiledGraph.validFor(graph)
        || !compiledGraph.validPacketRange(range)
        || (taskTimingTicketCount != 0u && !taskTimingTickets)
    )
        return false;

    Vector<GpuTaskGraphPacketTimingTicket, Alloc::ScratchArena> packetTimingTickets{ scratchArena };
    packetTimingTickets.reserve(taskTimingTicketCount);
    for(usize bindingIndex = 0u; bindingIndex < taskTimingTicketCount; ++bindingIndex){
        const GpuTaskGraphTaskTimingTicket& binding = taskTimingTickets[bindingIndex];
        if(
            !binding.timingTicket
            || !graph.validTask(binding.task)
            || !compiledGraph.findTask(binding.task)
        )
            return false;

        for(usize previousBindingIndex = 0u; previousBindingIndex < bindingIndex; ++previousBindingIndex){
            if(taskTimingTickets[previousBindingIndex].task == binding.task)
                return false;
        }

        const GpuSubmissionPacketId packet = compiledGraph.packetForTask(binding.task);
        if(
            !packet.valid()
            || packet.index < range.first.index
            || static_cast<usize>(packet.index) >= static_cast<usize>(range.first.index) + range.packetCount
        )
            return false;

        bool packetAlreadyBound = false;
        for(const GpuTaskGraphPacketTimingTicket& existing : packetTimingTickets){
            if(existing.packet != packet)
                continue;
            // A merged packet submits exactly one native command list.  Let semantic anchors intentionally share
            // that one ticket, but reject silently choosing between two independent timing transactions.
            if(existing.timingTicket != binding.timingTicket)
                return false;
            packetAlreadyBound = true;
            break;
        }
        if(!packetAlreadyBound){
            packetTimingTickets.push_back(GpuTaskGraphPacketTimingTicket{
                .packet = packet,
                .timingTicket = binding.timingTicket,
            });
        }
    }

    return submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        range,
        externalCompletionTokens,
        externalCompletionTokenCount,
        packetTimingTickets.data(),
        packetTimingTickets.size(),
        transaction,
        scratchArena,
        outFailedPacket,
        acceptedCallback,
        submissionHooks,
        submissionHookCount
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
