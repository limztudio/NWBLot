// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_recording{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_PacketMarkerLabel = "GPU Task Packet";
inline constexpr AStringView s_DefaultTaskMarkerLabel = "GPU Task";


[[nodiscard]] GpuTimingScopeDefinition TimingScopeDefinition(
    const Name& identity,
    const AStringView markerLabel
)noexcept{
    GpuTimingScopeDefinition definition;
    definition.identity = identity;
    definition.markerLabel = markerLabel;
    return definition;
}

[[nodiscard]] Name PacketTimingScopeName(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId& packet
){
    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packet);
    if(packetPlan.taskCount == 0u || !tasks)
        return NAME_NONE;
    return GpuTaskPacketTimingScopeName(graph.taskAt(tasks[0u].index).identity);
}

[[nodiscard]] bool CountTimingScopeOccurrences(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const Name& scopeName,
    usize& outOccurrenceCount
){
    outOccurrenceCount = 0u;
    const GpuSubmissionPacketRange packetTimingEnvelopeRange = compiledGraph.packetTimingEnvelopeRange();
    for(usize packetIndex = 0u; packetIndex < compiledGraph.packetCount(); ++packetIndex){
        const GpuSubmissionPacketId packetID = compiledGraph.packetIdAt(packetIndex);
        const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
        const GpuTaskId* const tasks = compiledGraph.packetTasks(packetID);
        if(packet.taskCount == 0u || !tasks)
            return false;

        bool recordsTiming = false;
        for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
            const GpuTaskId task = tasks[taskIndex];
            const GpuTaskGraphTaskView taskView = graph.taskAt(task.index);
            const GpuCompiledTask* const compiledTask = compiledGraph.findTask(task);
            if(
                !compiledTask
                || compiledTask->packet != packetID
                || compiledTask->timingPolicy != taskView.timing.policy
                || compiledTask->timingPolicy >= GpuTaskTimingPolicy::kCount
            )
                return false;
            recordsTiming = recordsTiming || compiledTask->timingPolicy != GpuTaskTimingPolicy::None;
            if(compiledTask->timingPolicy == GpuTaskTimingPolicy::Task && taskView.identity == scopeName)
                ++outOccurrenceCount;
        }
        const bool recordsPacketEnvelopeTiming = packetTimingEnvelopeRange.valid()
            && packetIndex >= packetTimingEnvelopeRange.first.index
            && packetIndex - packetTimingEnvelopeRange.first.index < packetTimingEnvelopeRange.packetCount
        ;
        if(packet.recordsPacketEnvelopeTiming != recordsPacketEnvelopeTiming)
            return false;
        recordsTiming = recordsTiming || recordsPacketEnvelopeTiming;
        if(packet.recordsTiming != recordsTiming)
            return false;
        if(packet.recordsTiming && PacketTimingScopeName(graph, compiledGraph, packetID) == scopeName)
            ++outOccurrenceCount;
    }
    return true;
}

[[nodiscard]] bool PrepareTimingScopeQueries(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    GpuTimingRecorder& timingRecorder,
    Device& device,
    const Name& scopeName
){
    usize occurrenceCount = 0u;
    if(
        !scopeName
        || !CountTimingScopeOccurrences(graph, compiledGraph, scopeName, occurrenceCount)
        || occurrenceCount == 0u
        || occurrenceCount > Limit<u32>::s_Max / s_MaxFramesInFlight
    )
        return false;
    return timingRecorder.prepareScopeQueries(
        scopeName,
        device,
        static_cast<u32>(occurrenceCount) * s_MaxFramesInFlight
    );
}

[[nodiscard]] bool PrepareCompiledTimingQueries(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    GpuTimingRecorder* const timingRecorder,
    Device& device
){
    bool recordsTiming = false;
    for(usize packetIndex = 0u; packetIndex < compiledGraph.packetCount(); ++packetIndex)
        recordsTiming = recordsTiming || compiledGraph.packet(compiledGraph.packetIdAt(packetIndex)).recordsTiming;
    if(!recordsTiming)
        return true;
    if(!timingRecorder)
        return false;

    for(usize packetIndex = 0u; packetIndex < compiledGraph.packetCount(); ++packetIndex){
        const GpuSubmissionPacketId packetID = compiledGraph.packetIdAt(packetIndex);
        const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
        if(!packet.recordsTiming)
            continue;
        const Name packetScopeName = PacketTimingScopeName(graph, compiledGraph, packetID);
        if(!PrepareTimingScopeQueries(graph, compiledGraph, *timingRecorder, device, packetScopeName))
            return false;

        const GpuTaskId* const tasks = compiledGraph.packetTasks(packetID);
        if(!tasks)
            return false;
        for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
            const GpuTaskId task = tasks[taskIndex];
            const GpuCompiledTask* const compiledTask = compiledGraph.findTask(task);
            if(!compiledTask)
                return false;
            if(
                compiledTask->timingPolicy == GpuTaskTimingPolicy::Task
                && !PrepareTimingScopeQueries(
                    graph,
                    compiledGraph,
                    *timingRecorder,
                    device,
                    graph.taskAt(task.index).identity
                )
            )
                return false;
        }
    }
    return true;
}


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

        const TextureDesc& description = texture->getCreationDescription();
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuNativePacketRecorder::recordPacket(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packetID,
    GpuRecordedGraph& outRecordedGraph,
    GpuRecordedGraph::PacketRecordingScratch& scratch,
    GpuCommandIrCapture* const commandIrCapture,
    const u64 recordingWorkerDomain,
    const u32 recordingWorkerIndex
)const{
    if(
        !compiledGraph.validFor(graph)
        || !graph.validForDeviceGeneration(compiledGraph.deviceGeneration())
        || m_device.getDeviceGeneration() != compiledGraph.deviceGeneration()
        || !compiledGraph.validPacket(packetID)
        || !outRecordedGraph.validFor(graph, compiledGraph)
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
        && commandIrCapture->recordCount() != 0u
        && commandIrCapture->recordingAttemptGeneration()
            != outRecordedGraph.recordingAttemptGeneration()
    )
        return false;
    if(outRecordedGraph.find(packetID))
        return false;

    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packetID);
    const GpuPhysicalQueueInfo* const queue = compiledGraph.queueInfo(packet.queue);
    GpuTimingSubmissionTicket* const packetTimingTicket = outRecordedGraph.packetTimingTicket(packetID);
    if(
        !tasks
        || packet.taskCount == 0u
        || !queue
        || queue->queueClass >= CommandQueue::kCount
        || !m_device.matchesPhysicalQueueIdentity(packet.queue)
        || packet.recordsTiming != static_cast<bool>(packetTimingTicket)
        || (
            packetTimingTicket
            && (
                !m_timingRecorder
                || outRecordedGraph.m_timingRecorder != m_timingRecorder
            )
        )
    )
        return false;
    const CommandListResourceStateHandoff* initialStates = nullptr;
    GpuTaskGraph::PacketRecordingLease recordingLease;
    const auto abortPacketRecording = [&]{
        outRecordedGraph.discardPacketTimingTicket(packetID);
        graph.abortPacketRecording(compiledGraph, packetID, recordingLease);
    };
    if(!graph.beginPacketRecording(
        compiledGraph,
        packetID,
        outRecordedGraph.recordingAttemptGeneration(),
        recordingLease
    ))
        return false;
    const Timer recordingBegin = TimerNow();

    if(!outRecordedGraph.buildPacketInitialStateSeed(
        scratch,
        graph,
        compiledGraph,
        packetID,
        initialStates
    )){
        abortPacketRecording();
        return false;
    }

    CommandListResourceStateHandoff* const packetStateSeed = outRecordedGraph.packetStateSeed(packetID);
    if(!packetStateSeed){
        abortPacketRecording();
        return false;
    }
    packetStateSeed->reset();

    if(!preflightPacketResources(graph, compiledGraph, packetID, initialStates)){
        packetStateSeed->reset();
        abortPacketRecording();
        return false;
    }

    const usize captureRecordCount = commandIrCapture ? commandIrCapture->recordCount() : 0u;
    if(
        commandIrCapture
        && !commandIrCapture->beginRecordingAttempt(outRecordedGraph.recordingAttemptGeneration())
    ){
        packetStateSeed->reset();
        abortPacketRecording();
        return false;
    }

    CommandListParameters parameters;
    parameters.setPhysicalQueue(packet.queue);
    parameters.setRecordingWorker(recordingWorkerDomain, recordingWorkerIndex);
    const Timer commandListAcquisitionBegin = TimerNow();
    CommandListHandle commandList = m_device.createCommandList(parameters);
    if(!commandList){
        packetStateSeed->reset();
        abortPacketRecording();
        if(commandIrCapture)
            commandIrCapture->rollback(captureRecordCount);
        return false;
    }

    commandList->open(initialStates);
    const f64 commandListAcquisitionSeconds = DurationInSeconds<f64>(TimerNow(), commandListAcquisitionBegin);
    bool recorded = commandList->hasCommandBuffer();
    const u64 packetRecordingLeaseSerial = commandList->recordingLeaseSerial();
    bool packetRecordingLeaseIntact = commandList->matchesRecordingLease(packetRecordingLeaseSerial);
    Optional<GpuTimingSubmissionTicket::RecordingScope> packetTimingRecordingScope;
    Optional<GpuTimingMeasure> packetTiming;
    bool packetMarkerStarted = false;
    if(recorded && packetTimingTicket){
        packetTimingRecordingScope.emplace(*packetTimingTicket);
        packetTiming.emplace(
            *m_timingRecorder,
            __hidden_gpu_packet_runtime_recording::TimingScopeDefinition(
                __hidden_gpu_packet_runtime_recording::PacketTimingScopeName(graph, compiledGraph, packetID),
                __hidden_gpu_packet_runtime_recording::s_PacketMarkerLabel
            ),
            m_device,
            *commandList
        );
    }
    else if(recorded){
        commandList->beginMarker(__hidden_gpu_packet_runtime_recording::s_PacketMarkerLabel);
    }
    packetRecordingLeaseIntact = commandList->matchesRecordingLease(packetRecordingLeaseSerial);
    if(!packetTimingTicket)
        packetMarkerStarted = packetRecordingLeaseIntact && !commandList->commandRecordingFailed();
    if(recorded && (!packetRecordingLeaseIntact || commandList->commandRecordingFailed()))
        recorded = false;
    u32 barrierCount = 0u;
    f64 graphBarrierRecordingSeconds = 0.0;
    f64 taskRecordSeconds = 0.0;
    for(u32 taskIndex = 0u; recorded && taskIndex < packet.taskCount; ++taskIndex){
        const GpuTaskId task = tasks[taskIndex];
        const GpuTaskGraphTaskView taskView = graph.taskAt(task.index);
        const GpuCompiledTask* const compiledTask = compiledGraph.findTask(task);
        if(
            !compiledTask
            || compiledTask->packet != packetID
            || compiledTask->timingPolicy != taskView.timing.policy
        ){
            recorded = false;
            break;
        }
        const GpuTaskRecordContext context{
            .taskGraph = graph,
            .graph = compiledGraph,
            .task = task,
            .packet = packetID,
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
        const u64 taskRecordingLeaseSerial = commandList->recordingLeaseSerial();
        bool taskRecordingLeaseIntact = commandList->matchesRecordingLease(taskRecordingLeaseSerial);
        if(recorded && (!taskRecordingLeaseIntact || commandList->commandRecordingFailed())){
            NWB_LOGGER_CRITICAL_WARNING(
                NWB_TEXT("Gpu task graph: rejecting task '{}' because its prologue invalidated or replaced the native command buffer"),
                StringConvert(taskView.markerLabel)
            );
            recorded = false;
        }
        Optional<GpuTimingMeasure> taskTiming;
        bool taskMarkerStarted = false;
        if(recorded){
            if(compiledTask->timingPolicy == GpuTaskTimingPolicy::Task){
                taskTiming.emplace(
                    *m_timingRecorder,
                    __hidden_gpu_packet_runtime_recording::TimingScopeDefinition(
                        taskView.identity,
                        taskView.markerLabel.empty()
                            ? __hidden_gpu_packet_runtime_recording::s_DefaultTaskMarkerLabel
                            : taskView.markerLabel
                    ),
                    m_device,
                    *commandList
                );
            }
            else{
                commandList->beginMarker(taskView.markerLabel);
            }
            taskRecordingLeaseIntact = commandList->matchesRecordingLease(taskRecordingLeaseSerial);
            if(compiledTask->timingPolicy != GpuTaskTimingPolicy::Task)
                taskMarkerStarted = taskRecordingLeaseIntact && !commandList->commandRecordingFailed();
            if(!taskRecordingLeaseIntact || commandList->commandRecordingFailed()){
                NWB_LOGGER_CRITICAL_WARNING(
                    NWB_TEXT("Gpu task graph: rejecting task '{}' because its marker invalidated or replaced the native command buffer"),
                    StringConvert(taskView.markerLabel)
                );
                recorded = false;
            }
        }
#if defined(NWB_DEBUG)
        bool taskCapabilityTrackingStarted = false;
        if(recorded){
            commandList->beginTaskCapabilityTracking(taskView.queue.requiredCapabilities);
            taskCapabilityTrackingStarted = true;
        }
#endif
        if(recorded){
            const Timer taskRecordBegin = TimerNow();
            bool recordThunkInvoked = false;
            recorded = graph.recordTask(task, *commandList, context, recordingLease, recordThunkInvoked);
            taskRecordSeconds += DurationInSeconds<f64>(TimerNow(), taskRecordBegin);
            taskRecordingLeaseIntact = commandList->matchesRecordingLease(taskRecordingLeaseSerial);
            if(!taskRecordingLeaseIntact){
                NWB_LOGGER_CRITICAL_WARNING(
                    NWB_TEXT("Gpu task graph: rejecting task '{}' because its record thunk closed or replaced the native command buffer"),
                    StringConvert(taskView.markerLabel)
                );
                recorded = false;
            }
            else if(commandList->commandRecordingFailed()){
                NWB_LOGGER_CRITICAL_WARNING(
                    NWB_TEXT("Gpu task graph: rejecting task '{}' because native command recording failed on exact queue {}:{}"),
                    StringConvert(taskView.markerLabel),
                    packet.queue.index,
                    packet.queue.deviceGeneration
                );
                recorded = false;
            }
            else if(!recorded && recordThunkInvoked){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Gpu task graph: semantic record thunk for task identity '{}' marker '{}' returned false for packet {}:{} on assigned physical queue class {} index {} device generation {}")
                    , StringConvert(taskView.identity.c_str())
                    , StringConvert(taskView.markerLabel)
                    , packetID.index
                    , packetID.generation
                    , static_cast<u32>(queue->queueClass)
                    , queue->id.index
                    , queue->id.deviceGeneration
                );
            }
        }
        if(recorded)
            commandList->endRenderPass();
        taskRecordingLeaseIntact = commandList->matchesRecordingLease(taskRecordingLeaseSerial);
        if(recorded && (!taskRecordingLeaseIntact || commandList->commandRecordingFailed()))
            recorded = false;
#if defined(NWB_DEBUG)
        GpuQueueCapability::Mask usedCapabilities = GpuQueueCapability::None;
        if(taskCapabilityTrackingStarted){
            if(taskRecordingLeaseIntact)
                usedCapabilities = commandList->endTaskCapabilityTracking();
            else
                commandList->cancelTaskCapabilityTracking();
        }
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
        if(taskTiming.has_value()){
            if(taskRecordingLeaseIntact){
                if(recorded)
                    taskTiming.value().finishTiming(*commandList);
                else
                    taskTiming.value().discardTiming();
                taskTiming.value().finishMarker();
            }
            else{
                taskTiming.value().discardTiming();
                taskTiming.value().abandonMarker();
            }
            taskTiming.reset();
        }
        else if(taskMarkerStarted && taskRecordingLeaseIntact)
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
    packetRecordingLeaseIntact = commandList->matchesRecordingLease(packetRecordingLeaseSerial);
    if(recorded && (!packetRecordingLeaseIntact || commandList->commandRecordingFailed()))
        recorded = false;
    if(packetTiming.has_value()){
        if(packetRecordingLeaseIntact){
            if(recorded)
                packetTiming.value().finishTiming(*commandList);
            else
                packetTiming.value().discardTiming();
            packetTiming.value().finishMarker();
        }
        else{
            packetTiming.value().discardTiming();
            packetTiming.value().abandonMarker();
        }
        packetTiming.reset();
    }
    else if(packetMarkerStarted && packetRecordingLeaseIntact)
        commandList->endMarker();
    packetTimingRecordingScope.reset();
    if(recorded && commandList->commandRecordingFailed())
        recorded = false;
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
        packetID,
        recordingLease
    )){
        packetStateSeed->reset();
        abortPacketRecording();
        if(commandIrCapture)
            commandIrCapture->rollback(captureRecordCount);
        return false;
    }
    const Timer recordingEnd = TimerNow();
    GpuRecordedPacket& recordedPacket = outRecordedGraph.m_packets[packetID.index];
    recordedPacket.packet = packetID;
    recordedPacket.commandLists[0u] = commandList.get();
    recordedPacket.ownedCommandLists[0u] = Move(commandList);
    recordedPacket.taskCount = packet.taskCount;
    recordedPacket.barrierCount = barrierCount;
    recordedPacket.commandListAcquisitionSeconds = commandListAcquisitionSeconds;
    recordedPacket.graphBarrierRecordingSeconds = graphBarrierRecordingSeconds;
    recordedPacket.taskRecordSeconds = taskRecordSeconds;
    recordedPacket.recordingBeginNanoseconds = DurationInNS<u64>(recordingBegin);
    recordedPacket.recordingEndNanoseconds = DurationInNS<u64>(recordingEnd);
    recordedPacket.recordingSeconds = DurationInSeconds<f64>(recordingEnd, recordingBegin);
    recordedPacket.recordingWorkerDomain = recordingWorkerDomain;
    recordedPacket.recordingWorkerIndex = recordingWorkerIndex;
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
    if(!__hidden_gpu_packet_runtime_recording::PrepareCompiledTimingQueries(
        graph,
        compiledGraph,
        m_timingRecorder,
        m_device
    ))
        return false;

    const usize rangeBegin = range.first.index;
    const usize rangeEnd = rangeBegin + range.packetCount;
    for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packetID = compiledGraph.packetIdAt(packetIndex);
        if(!graph.beginRecordingAttempt(compiledGraph, packetID))
            return false;
    }

    if(!outRecordedGraph.validFor(graph, compiledGraph))
        outRecordedGraph.resetForRecording(graph, compiledGraph);
    if(
        m_timingRecorder
        && !outRecordedGraph.prepareTimingTickets(compiledGraph, *m_timingRecorder)
    )
        return false;
    return outRecordedGraph.validFor(graph, compiledGraph);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

