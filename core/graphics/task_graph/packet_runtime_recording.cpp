// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/capture/command_ir.h>

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_recording{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool HasExplicitKnownInitialState(
    const GpuTaskGraph& graph,
    const GpuCompiledBarrier& barrier,
    CommandList& commandList
){
    if(!graph.validResource(barrier.resource))
        return false;

    const GpuTaskGraphResourceView resource = graph.resourceAt(barrier.resource.index);
    if(resource.id != barrier.resource || !resource.hasBackendResource)
        return false;

    switch(resource.type){
    case GpuGraphResourceType::Texture:{
        Texture* const texture = graph.textureForResource(barrier.resource);
        if(!texture)
            return false;

        const TextureDesc& description = texture->getDescription();
        const TextureSubresourceSet subresources = barrier.range.textureSubresources.resolve(
            description,
            TextureSubresourceMipResolve::Range
        );
        const u64 mipEnd = static_cast<u64>(subresources.baseMipLevel) + subresources.numMipLevels;
        const u64 arrayEnd = static_cast<u64>(subresources.baseArraySlice) + subresources.numArraySlices;
        if(
            subresources.numMipLevels == 0u
            || subresources.numArraySlices == 0u
            || mipEnd > description.mipLevels
            || arrayEnd > description.arraySize
        )
            return false;

        for(ArraySlice arraySlice = subresources.baseArraySlice;
            static_cast<u64>(arraySlice) < arrayEnd;
            ++arraySlice
        ){
            for(MipLevel mipLevel = subresources.baseMipLevel;
                static_cast<u64>(mipLevel) < mipEnd;
                ++mipLevel
            ){
                if(
                    !commandList.hasExplicitTextureSubresourceState(texture, arraySlice, mipLevel)
                    || commandList.getTextureSubresourceState(texture, arraySlice, mipLevel) == ResourceStates::Unknown
                )
                    return false;
            }
        }
        return true;
    }
    case GpuGraphResourceType::Buffer:{
        Buffer* const buffer = graph.bufferForResource(barrier.resource);
        return buffer
            && commandList.hasExplicitBufferState(buffer)
            && commandList.getBufferState(buffer) != ResourceStates::Unknown
        ;
    }
    case GpuGraphResourceType::AccelStruct:{
        RayTracingAccelStruct* const accelStruct = graph.accelStructForResource(barrier.resource);
        Buffer* const backingBuffer = accelStruct ? accelStruct->getBackingBuffer() : nullptr;
        return backingBuffer
            && commandList.hasExplicitBufferState(backingBuffer)
            && commandList.getBufferState(backingBuffer) != ResourceStates::Unknown
        ;
    }
    default:
        return false;
    }
}

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
            if(!states || !states->validForDeviceGeneration(compiledGraph.deviceGeneration()))
                return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        || !graph.validForDeviceGeneration(compiledGraph.deviceGeneration())
        || m_device.getDeviceGeneration() != compiledGraph.deviceGeneration()
        || !compiledGraph.validPacket(desc.packet)
        || !__hidden_gpu_packet_runtime_recording::ValidateTaskPacketStateBindings(
            graph,
            compiledGraph,
            taskStateBindings,
            taskStateBindingCount
        )
    )
        return false;
    if(!prepareRecordingAttempt(
        graph,
        compiledGraph,
        GpuSubmissionPacketRange{ .first = desc.packet, .packetCount = 1u },
        outRecordedGraph
    ))
        return false;
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
        || !graph.validForDeviceGeneration(compiledGraph.deviceGeneration())
        || m_device.getDeviceGeneration() != compiledGraph.deviceGeneration()
        || !compiledGraph.validPacket(desc.packet)
        || !outRecordedGraph.validFor(graph, compiledGraph)
        || !__hidden_gpu_packet_runtime_recording::ValidateTaskPacketStateBindings(
            graph,
            compiledGraph,
            taskStateBindings,
            taskStateBindingCount
        )
    )
        return false;
    // A capture is one immutable compiled-plan artifact. Reject a stale non-empty capture before opening a packet
    // that happens not to contain a primitive command; otherwise old records could be mistaken for this packet's
    // trace after the same graph is recompiled with a different packet/queue plan.
    if(
        commandIrCapture
        && commandIrCapture->recordCount() != 0u
        && (
            commandIrCapture->graphGeneration() != compiledGraph.generation()
            || commandIrCapture->planGeneration() != compiledGraph.planGeneration()
        )
    )
        return false;
    if(
        commandIrCapture
        && !commandIrCapture->beginRecordingAttempt(outRecordedGraph.recordingAttemptGeneration())
    )
        return false;
    if(outRecordedGraph.find(desc.packet))
        return false;

    const GpuSubmissionPacket& packet = compiledGraph.packet(desc.packet);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(desc.packet);
    GpuTaskPacketRecordingLease recordingLease;
    const auto abortPacketRecording = [&]{
        graph.abortPacketRecording(
            compiledGraph,
            tasks,
            packet.taskCount,
            recordingLease
        );
    };

    if(!graph.beginPacketRecording(
        compiledGraph,
        tasks,
        packet.taskCount,
        outRecordedGraph.recordingAttemptGeneration(),
        recordingLease
    ))
        return false;
    const Timer recordingBegin = TimerNow();

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
    {
        abortPacketRecording();
        return false;
    }

    CommandListResourceStateHandoff* const packetStateSeed = outRecordedGraph.packetStateSeed(desc.packet);
    if(!packetStateSeed){
        abortPacketRecording();
        return false;
    }
    packetStateSeed->reset();

    const GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfo(packet.queue);
    if(
        !queue
        || queue->queueClass >= CommandQueue::kCount
        || !m_device.matchesPhysicalQueueIdentity(packet.queue)
    )
    {
        abortPacketRecording();
        return false;
    }

    const usize captureRecordCount = commandIrCapture ? commandIrCapture->recordCount() : 0u;

    CommandListParameters parameters;
    parameters.setPhysicalQueue(packet.queue);
    parameters.setRecordingWorker(desc.recordingWorkerDomain, desc.recordingWorkerIndex);
    const Timer commandListAcquisitionBegin = TimerNow();
    CommandListHandle commandList = m_device.createCommandList(parameters);
    if(!commandList){
        abortPacketRecording();
        return false;
    }

    commandList->open(initialStates);
    const f64 commandListAcquisitionSeconds = DurationInSeconds<f64>(TimerNow(), commandListAcquisitionBegin);
    bool recorded = commandList->hasCommandBuffer();
    u32 barrierCount = 0u;
    f64 graphBarrierRecordingSeconds = 0.0;
    f64 taskRecordSeconds = 0.0;
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
            .recordingAttemptGeneration = outRecordedGraph.recordingAttemptGeneration(),
            .commandIrCapture = commandIrCapture,
        };
        const GpuCompiledBarrier* const prologueBarriers = compiledGraph.taskPrologueBarriers(task);
        if(compiledTask->prologueBarrierCount > 0u && !prologueBarriers){
            recorded = false;
            break;
        }
        barrierCount += compiledTask->prologueBarrierCount;
        const Timer graphPrologueRecordingBegin = TimerNow();
        for(u32 barrierIndex = 0u; recorded && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = prologueBarriers[barrierIndex];
            if(barrier.isGraphInitialState && barrier.before == ResourceStates::Unknown){
                if(!__hidden_gpu_packet_runtime_recording::HasExplicitKnownInitialState(graph, barrier, *commandList)){
                    const GpuTaskGraphResourceView resource = graph.resourceAt(barrier.resource.index);
                    NWB_LOGGER_ERROR(
                        NWB_TEXT("Gpu task graph: rejecting task '{}' because first-read resource '{}' has no explicit initial native state source")
                        , StringConvert(taskView.markerLabel)
                        , StringConvert(resource.markerLabel)
                    );
                    recorded = false;
                    break;
                }

                // The immutable marker made record-time validation mandatory. The imported source is now
                // authoritative, so lower a local copy without asking graph-initial lowering to seed Unknown.
                GpuCompiledBarrier loweredBarrier = barrier;
                loweredBarrier.isGraphInitialState = false;
                recorded = graph.applyCompiledBarrier(compiledGraph, loweredBarrier, *commandList);
            }
            else
                recorded = graph.applyCompiledBarrier(compiledGraph, barrier, *commandList);
        }
        // A retained state that already matches the compiler plan still needs a native tracker entry. Otherwise a
        // later packet cannot import that graph-declared resource state, and a renderer thunk would need a redundant
        // direct transition merely to publish its handoff.
        if(recorded)
            recorded = graph.seedTaskRetainedResourceStates(task, *commandList);
        if(recorded)
            commandList->commitBarriers();
        graphBarrierRecordingSeconds += DurationInSeconds<f64>(TimerNow(), graphPrologueRecordingBegin);
        commandList->beginMarker(taskView.markerLabel);
#if defined(NWB_DEBUG)
        commandList->beginTaskCapabilityTracking();
#endif
        if(recorded){
            const Timer taskRecordBegin = TimerNow();
            recorded = graph.recordTask(task, *commandList, context, recordingLease);
            taskRecordSeconds += DurationInSeconds<f64>(TimerNow(), taskRecordBegin);
        }
        if(recorded)
            commandList->endRenderPass();
#if defined(NWB_DEBUG)
        const GpuQueueCapability::Mask usedCapabilities = commandList->endTaskCapabilityTracking();
        if(
            recorded
            && (
                !__hidden_gpu_packet_runtime_recording::HasQueueCapabilities(
                    taskView.queue.requiredCapabilities,
                    usedCapabilities
                )
                || !__hidden_gpu_packet_runtime_recording::HasQueueCapabilities(queue->capabilities, usedCapabilities)
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
        const Timer graphEpilogueRecordingBegin = TimerNow();
        for(u32 barrierIndex = 0u; recorded && barrierIndex < compiledTask->epilogueBarrierCount; ++barrierIndex)
            recorded = graph.applyCompiledBarrier(compiledGraph, epilogueBarriers[barrierIndex], *commandList);
        barrierCount += compiledTask->epilogueBarrierCount;
        if(recorded)
            commandList->commitBarriers();
        graphBarrierRecordingSeconds += DurationInSeconds<f64>(TimerNow(), graphEpilogueRecordingBegin);
    }
    commandList->close(packetStateSeed);
    if(!recorded || !commandList->hasCommandBuffer()){
        packetStateSeed->reset();
        abortPacketRecording();
        if(commandIrCapture)
            commandIrCapture->rollback(captureRecordCount);
        return false;
    }
    if(!graph.completePacketRecording(
        compiledGraph,
        tasks,
        packet.taskCount,
        recordingLease
    )){
        packetStateSeed->reset();
        abortPacketRecording();
        if(commandIrCapture)
            commandIrCapture->rollback(captureRecordCount);
        return false;
    }
    GpuRecordedPacket& recordedPacket = outRecordedGraph.m_packets[desc.packet.index];
    recordedPacket.packet = desc.packet;
    recordedPacket.commandLists[0u] = commandList.get();
    recordedPacket.ownedCommandLists[0u] = Move(commandList);
    recordedPacket.taskCount = packet.taskCount;
    recordedPacket.barrierCount = barrierCount;
    recordedPacket.commandListAcquisitionSeconds = commandListAcquisitionSeconds;
    recordedPacket.graphBarrierRecordingSeconds = graphBarrierRecordingSeconds;
    recordedPacket.taskRecordSeconds = taskRecordSeconds;
    recordedPacket.recordingSeconds = DurationInSeconds<f64>(TimerNow(), recordingBegin);
    recordedPacket.recordingWorkerDomain = desc.recordingWorkerDomain;
    recordedPacket.recordingWorkerIndex = desc.recordingWorkerIndex;
    // Publish the slot only after its owned native list is retained. Frontier workers are joined before callers can
    // submit, but this order also keeps the slot self-consistent for diagnostic reads.
    recordedPacket.commandListCount = 1u;
    return true;
}

bool GpuNativePacketRecorder::prepareRecordingAttempt(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketRange& range,
    GpuRecordedGraph& outRecordedGraph
)const{
    if(!compiledGraph.validFor(graph) || !compiledGraph.validPacketRange(range))
        return false;

    const usize rangeBegin = range.first.index;
    const usize rangeEnd = rangeBegin + range.packetCount;
    for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packetID = compiledGraph.packetIdAt(packetIndex);
        const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
        const GpuTaskId* const tasks = compiledGraph.packetTasks(packetID);
        if(!tasks || packet.taskCount == 0u || !graph.beginRecordingAttempt(compiledGraph, tasks, packet.taskCount))
            return false;
    }

    if(!outRecordedGraph.validFor(graph, compiledGraph))
        outRecordedGraph.resetForRecording(graph, compiledGraph);
    return outRecordedGraph.validFor(graph, compiledGraph);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

