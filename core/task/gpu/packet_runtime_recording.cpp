// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"

#include <core/graphics/backend_selection.h>
#include <core/task/gpu/capture/command_ir.h>
#include <core/graphics/gpu_timing.h>

#include <global/exception.h>
#include <global/termination.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuNativePacketRecorder::PacketArtifactPublicationScope final : NoCopy{
public:
    PacketArtifactPublicationScope(
        GpuRecordedGraph& recordedGraph,
        const GpuSubmissionPacketId packet,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess
    )noexcept
        : m_recordedGraph(recordedGraph)
        , m_artifactAccess(artifactAccess)
        , m_packet(packet)
    {}
    ~PacketArtifactPublicationScope()noexcept{
        if(m_active)
            m_recordedGraph.clearPacketPublicationWithoutCallbacks(m_packet, m_artifactAccess);
    }


public:
    void complete()noexcept{ m_active = false; }


private:
    GpuRecordedGraph& m_recordedGraph;
    const GpuRecordedGraph::ArtifactOperation& m_artifactAccess;
    GpuSubmissionPacketId m_packet;
    bool m_active = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_recording{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_PacketTimingScratchArena("core/task/gpu/packet_timing_scratch");
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
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId& packet
){
    const GpuCompiledPacketView packetView = planAccess.packet(packet);
    if(!packetView.valid() || packetView.plan->taskCount == 0u)
        return NAME_NONE;
    const GpuTaskGraphTaskView task = declarationAccess.taskAt(packetView.tasks[0u].index);
    return task.id == packetView.tasks[0u] ? GpuTaskPacketTimingScopeName(task.identity) : NAME_NONE;
}

[[nodiscard]] bool PrepareCompiledTimingQueries(
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph::ReadView& planAccess,
    GpuTimingRecorder* const timingRecorder,
    Alloc::ScratchArena& scratchArena){
    bool recordsTiming = false;
    for(usize packetIndex = 0u; packetIndex < planAccess.packetCount(); ++packetIndex){
        const GpuCompiledPacketView packetView = planAccess.packet(planAccess.packetIdAt(packetIndex));
        if(!packetView.valid())
            return false;
        if(packetView.plan->recordsTiming){
            recordsTiming = true;
            break;
        }
    }
    if(!recordsTiming)
        return true;
    if(!timingRecorder)
        return false;

    HashMap<Name, u32, Hasher<Name>, EqualTo<Name>, Alloc::ScratchArena> scopeOccurrences(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        scratchArena
    );
    const usize maxScopeCount = declarationAccess.taskCount() + planAccess.packetCount();
    scopeOccurrences.reserve(maxScopeCount);
    Vector<Name, Alloc::ScratchArena> scopeOrder(scratchArena);
    scopeOrder.reserve(maxScopeCount);
    const auto countScopeOccurrence = [&](const Name& scopeName){
        if(!scopeName)
            return false;
        auto [it, inserted] = scopeOccurrences.try_emplace(scopeName, 0u);
        if(inserted)
            scopeOrder.push_back(scopeName);
        u32& occurrenceCount = it.value();
        if(occurrenceCount >= Limit<u32>::s_Max / s_MaxFramesInFlight)
            return false;
        ++occurrenceCount;
        return true;
    };

    // Task and packet scopes share the timing recorder's identity namespace. Count both together once so repeated
    // tasks and an authored task identity matching another packet's derived identity reserve their full demand.
    const GpuSubmissionPacketRange packetTimingEnvelopeRange = planAccess.packetTimingEnvelopeRange();
    for(usize packetIndex = 0u; packetIndex < planAccess.packetCount(); ++packetIndex){
        const GpuSubmissionPacketId packetID = planAccess.packetIdAt(packetIndex);
        const GpuCompiledPacketView packetView = planAccess.packet(packetID);
        if(!packetView.valid() || packetView.plan->taskCount == 0u)
            return false;
        const GpuSubmissionPacket& packet = *packetView.plan;
        const GpuTaskId* const tasks = packetView.tasks;

        // Scope IDs and report rows follow first registration, with each packet preceding its task scopes.
        if(packet.recordsTiming && !countScopeOccurrence(PacketTimingScopeName(declarationAccess, planAccess, packetID)))
            return false;
        bool packetRecordsTiming = false;
        for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
            const GpuTaskId task = tasks[taskIndex];
            const GpuTaskGraphTaskView taskView = declarationAccess.taskAt(task.index);
            const GpuCompiledTaskView compiledTaskView = planAccess.findTask(task);
            const GpuCompiledTask* const compiledTask = compiledTaskView.plan;
            if(
                !compiledTaskView.valid()
                || taskView.id != task
                || compiledTask->packet != packetID
                || compiledTask->timingPolicy != taskView.timing.policy
                || compiledTask->timingPolicy >= GpuTaskTimingPolicy::kCount
            )
                return false;
            packetRecordsTiming = packetRecordsTiming || compiledTask->timingPolicy != GpuTaskTimingPolicy::None;
            if(compiledTask->timingPolicy == GpuTaskTimingPolicy::Task && !countScopeOccurrence(taskView.identity))
                return false;
        }
        const bool recordsPacketEnvelopeTiming = packetTimingEnvelopeRange.valid()
            && packetIndex >= packetTimingEnvelopeRange.first.index
            && packetIndex - packetTimingEnvelopeRange.first.index < packetTimingEnvelopeRange.packetCount
        ;
        if(packet.recordsPacketEnvelopeTiming != recordsPacketEnvelopeTiming)
            return false;
        packetRecordsTiming = packetRecordsTiming || recordsPacketEnvelopeTiming;
        if(packet.recordsTiming != packetRecordsTiming)
            return false;
    }
    for(const Name& scopeName : scopeOrder){
        const u32 occurrenceCount = scopeOccurrences.find(scopeName).value();
        // Recording runs inside render/submission: declare demand only. The frame preamble owns GPU pool
        // creation through materializeRequestedQueries(), so this path never calls device.createTimerQuery().
        if(!timingRecorder->requestScopeQueries(scopeName, occurrenceCount * s_MaxFramesInFlight))
            return false;
    }
    return true;
}

[[nodiscard]] static bool HasExplicitKnownInitialState(
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledBarrier& barrier,
    CommandList& commandList
){
    if(!declarationAccess.validResource(barrier.resource))
        return false;

    const GpuTaskGraphResourceView resource = declarationAccess.resourceAt(barrier.resource.index);
    if(resource.id != barrier.resource || !resource.hasBackendResource)
        return false;

    switch(resource.type){
    case GpuGraphResourceType::Texture:{
        Texture* const texture = declarationAccess.textureForResource(barrier.resource);
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
        Buffer* const buffer = declarationAccess.bufferForResource(barrier.resource);
        return buffer
            && commandList.hasExplicitBufferState(buffer, barrier.range.bufferRange, true)
        ;
    }
    case GpuGraphResourceType::AccelStruct:{
        RayTracingAccelStruct* const accelStruct = declarationAccess.accelStructForResource(barrier.resource);
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


class GpuNativePacketRecorder::PacketRecordingExceptionScope final : NoCopy{
public:
    PacketRecordingExceptionScope(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuRecordedGraph& recordedGraph,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
        const GpuSubmissionPacketId packet,
        const u64 recordingAttemptGeneration,
        GpuTaskGraph::PacketRecordingLease& recordingLease,
        GpuTaskGraph::PacketRecordingAbort* const deferredAbort,
        GpuCommandIrCapture* const commandIrCapture,
        const usize captureRecordCount,
        CommandListResourceStateHandoff*& packetStateSeed
    )noexcept
        : m_graph(graph)
        , m_compiledGraph(compiledGraph)
        , m_planAccess(planAccess)
        , m_recordedGraph(recordedGraph)
        , m_artifactAccess(artifactAccess)
        , m_recordingLease(recordingLease)
        , m_packetStateSeed(packetStateSeed)
        , m_deferredAbort(deferredAbort)
        , m_commandIrCapture(commandIrCapture)
        , m_packet(packet)
        , m_recordingAttemptGeneration(recordingAttemptGeneration)
        , m_captureRecordCount(captureRecordCount)
        , m_uncaughtExceptionCount(UncaughtExceptionCount())
    {}
    ~PacketRecordingExceptionScope()noexcept{
        static_assert(noexcept(static_cast<CommandListResourceStateHandoff*>(nullptr)->reset()));
        static_assert(noexcept(static_cast<GpuCommandIrCapture*>(nullptr)->rollback(0u)));

        if(UncaughtExceptionCount() <= m_uncaughtExceptionCount)
            return;
        if(m_packetStateSeed)
            m_packetStateSeed->reset();
        if(m_commandIrCapture)
            m_commandIrCapture->rollback(m_captureRecordCount);
        if(m_recordingLease.valid()){
            if(m_deferredAbort){
                const bool abortDeferred = m_graph.deferPacketRecordingAbort(
                    m_compiledGraph,
                    m_planAccess,
                    m_packet,
                    m_recordingLease,
                    *m_deferredAbort
                );
                NWB_FATAL_ASSERT_MSG(abortDeferred, "throwing packet recorder must transfer its exact recording claim");
                if(!abortDeferred)
                    TerminateInvariant();
            }
            else{
                m_recordedGraph.abandonPacketTimingTicketWithoutCallbacks(m_packet, m_artifactAccess);
                const bool recordingAbandoned = m_graph.abandonPacketRecordingWithoutCallbacks(
                    m_compiledGraph,
                    m_planAccess,
                    m_packet,
                    m_recordingLease
                );
                NWB_FATAL_ASSERT_MSG(recordingAbandoned, "throwing packet recorder must abandon its exact recording claim");
                if(!recordingAbandoned)
                    TerminateInvariant();
            }
        }
        if(m_deferredAbort)
            return;

        const bool recordingAttemptResolved = m_graph.resolveRecordingAttemptIfTerminal(
            m_compiledGraph,
            m_recordingAttemptGeneration
        );
        NWB_FATAL_ASSERT_MSG(
            recordingAttemptResolved,
            "throwing packet recorder must resolve a terminal recording-plan lease"
        );
        if(!recordingAttemptResolved)
            TerminateInvariant();
    }


private:
    const GpuTaskGraph& m_graph;
    const GpuCompiledGraph& m_compiledGraph;
    const GpuCompiledGraph::ReadView& m_planAccess;
    GpuRecordedGraph& m_recordedGraph;
    const GpuRecordedGraph::ArtifactOperation& m_artifactAccess;
    GpuTaskGraph::PacketRecordingLease& m_recordingLease;
    CommandListResourceStateHandoff*& m_packetStateSeed;
    GpuTaskGraph::PacketRecordingAbort* m_deferredAbort = nullptr;
    GpuCommandIrCapture* m_commandIrCapture = nullptr;
    GpuSubmissionPacketId m_packet;
    u64 m_recordingAttemptGeneration = 0u;
    usize m_captureRecordCount = 0u;
    i32 m_uncaughtExceptionCount = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuNativePacketRecorder::recordPacket(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuRecordedGraph::ArtifactOperation& artifactAccess,
    const GpuSubmissionPacketId packetID,
    GpuRecordedGraph& outRecordedGraph,
    GpuRecordedGraph::PacketRecordingScratch& scratch,
    Alloc::ScratchArena& stateFanInScratchArena,
    GpuCommandIrCapture* const commandIrCapture,
    const u64 recordingWorkerDomain,
    const u32 recordingWorkerIndex,
    GpuTaskGraph::PacketRecordingAbort* const deferredAbort
)const{
    if(deferredAbort && deferredAbort->valid()){
        NWB_ASSERT_MSG(false, NWB_TEXT("Packet recording abort output must be fresh"));
        return false;
    }
    if(!planAccess.validFor(compiledGraph) || !artifactAccess.exclusiveFor(outRecordedGraph))
        return false;
    GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::tryAcquire(graph);
    if(!declarationAccess.valid())
        return false;

    const u64 recordingAttemptGeneration =
        outRecordedGraph.recordingAttemptGenerationWithinArtifactOperation(artifactAccess)
    ;
    if(
        !graph.matchesRecordingAttempt(compiledGraph, recordingAttemptGeneration)
        || !planAccess.validFor(declarationAccess)
        || !declarationAccess.validForDeviceGeneration(planAccess.deviceGeneration())
        || m_device.getDeviceGeneration() != planAccess.deviceGeneration()
        || !planAccess.validPacket(packetID)
        || !outRecordedGraph.validForWithinArtifactOperation(
            graph,
            declarationAccess,
            compiledGraph,
            planAccess,
            artifactAccess
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
            commandIrCapture->graphGeneration() != planAccess.generation()
            || commandIrCapture->planGeneration() != planAccess.planGeneration()
        )
    )
        return false;
    if(
        commandIrCapture
        && commandIrCapture->recordCount() != 0u
        && commandIrCapture->recordingAttemptGeneration()
            != recordingAttemptGeneration
    )
        return false;
    if(outRecordedGraph.findWithinArtifactOperation(packetID, artifactAccess))
        return false;

    const GpuCompiledPacketView packetView = planAccess.packet(packetID);
    if(!packetView.valid())
        return false;
    const GpuSubmissionPacket& packet = *packetView.plan;
    const GpuTaskId* const tasks = packetView.tasks;
    const GpuPhysicalQueueInfo* const queue = planAccess.queueInfo(packet.queue);
    GpuTimingSubmissionTicket* const packetTimingTicket = outRecordedGraph.packetTimingTicket(packetID, artifactAccess);
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
                || outRecordedGraph.timingRecorderWithinArtifactOperation(artifactAccess) != m_timingRecorder
            )
        )
    )
        return false;
    const CommandListResourceStateHandoff* initialStates = nullptr;
    CommandListResourceStateHandoff* packetStateSeed = nullptr;
    const usize captureRecordCount = commandIrCapture ? commandIrCapture->recordCount() : 0u;
    GpuTaskGraph::PacketRecordingLease recordingLease;
    const auto abortPacketRecording = [&]{
        if(deferredAbort){
            const bool abortDeferred = graph.deferPacketRecordingAbort(
                compiledGraph,
                planAccess,
                packetID,
                recordingLease,
                *deferredAbort
            );
            NWB_FATAL_ASSERT_MSG(abortDeferred, "failed to transfer an active packet recording abort");
            if(!abortDeferred)
                TerminateInvariant();
            return;
        }
        outRecordedGraph.discardPacketTimingTicket(packetID, artifactAccess);
        graph.abortPacketRecording(compiledGraph, planAccess, packetID, recordingLease);
    };
    if(!graph.beginPacketRecording(
        compiledGraph,
        planAccess,
        packetID,
        recordingAttemptGeneration,
        recordingLease
    ))
        return false;
    const GpuTaskGraph::PacketRecordingAccess recordingAccess(graph, compiledGraph, planAccess, recordingLease);
    if(!recordingAccess.validFor(graph, compiledGraph, planAccess, packetID)){
        abortPacketRecording();
        return false;
    }
    PacketRecordingExceptionScope exceptionScope(
        graph,
        compiledGraph,
        planAccess,
        outRecordedGraph,
        artifactAccess,
        packetID,
        recordingAttemptGeneration,
        recordingLease,
        deferredAbort,
        commandIrCapture,
        captureRecordCount,
        packetStateSeed
    );
    const Timer recordingBegin = TimerNow();

    if(!outRecordedGraph.buildPacketInitialStateSeed(
        scratch,
        stateFanInScratchArena,
        graph,
        declarationAccess,
        compiledGraph,
        planAccess,
        artifactAccess,
        packetID,
        initialStates
    )){
        abortPacketRecording();
        return false;
    }

    packetStateSeed = outRecordedGraph.packetStateSeed(packetID, artifactAccess);
    if(!packetStateSeed){
        abortPacketRecording();
        return false;
    }
    packetStateSeed->reset();

    if(!preflightPacketResources(
        graph,
        declarationAccess,
        compiledGraph,
        planAccess,
        recordingAccess,
        packetID,
        initialStates
    )){
        packetStateSeed->reset();
        abortPacketRecording();
        return false;
    }

    if(
        commandIrCapture
        && !commandIrCapture->beginRecordingAttempt(recordingAttemptGeneration)
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
    const u64 packetRecordingLeaseSerial = commandList->recordingLeaseSerial();
    CommandList::GraphRecordingOwnership graphRecordingOwnership(*commandList, packetRecordingLeaseSerial);
    bool recorded = commandList->hasCommandBuffer() && !commandList->commandRecordingFailed();
    bool packetRecordingLeaseIntact = commandList->matchesRecordingLease(packetRecordingLeaseSerial);
    Optional<GpuTimingSubmissionTicket::RecordingScope> packetTimingRecordingScope;
    Optional<GpuTimingMeasure> packetTiming;
    CommandMarkerRecordingToken packetMarker;
    if(recorded && packetTimingTicket){
        packetTimingRecordingScope.emplace(*packetTimingTicket);
        packetTiming.emplace(
            *m_timingRecorder,
            __hidden_gpu_packet_runtime_recording::TimingScopeDefinition(
                __hidden_gpu_packet_runtime_recording::PacketTimingScopeName(declarationAccess, planAccess, packetID),
                __hidden_gpu_packet_runtime_recording::s_PacketMarkerLabel
            ),
            m_device,
            *commandList
        );
    }
    else if(recorded){
        packetMarker = commandList->beginMarkerLease(__hidden_gpu_packet_runtime_recording::s_PacketMarkerLabel);
    }
    packetRecordingLeaseIntact = commandList->matchesRecordingLease(packetRecordingLeaseSerial);
    if(recorded && (!packetRecordingLeaseIntact || commandList->commandRecordingFailed()))
        recorded = false;
    u32 barrierCount = 0u;
    f64 graphBarrierRecordingSeconds = 0.0;
    f64 taskRecordSeconds = 0.0;
    for(u32 taskIndex = 0u; recorded && taskIndex < packet.taskCount; ++taskIndex){
        const GpuTaskId task = tasks[taskIndex];
        const GpuTaskGraphTaskView taskView = declarationAccess.taskAt(task.index);
        const GpuCompiledTaskView compiledTaskView = planAccess.findTask(task);
        const GpuCompiledTask* const compiledTask = compiledTaskView.plan;
        if(
            !compiledTaskView.valid()
            || compiledTask->packet != packetID
            || compiledTask->timingPolicy != taskView.timing.policy
        ){
            recorded = false;
            break;
        }
        const GpuTaskRecordContext context{
            .declarations = declarationAccess,
            .compiledPlan = planAccess,
            .task = task,
            .packet = packetID,
            .queue = packet.queue,
            .recordingAttemptGeneration = recordingAttemptGeneration,
            .commandIrCapture = commandIrCapture,
        };
        const GpuCompiledBarrier* const prologueBarriers = compiledTaskView.prologueBarriers;
        barrierCount += compiledTask->prologueBarrierCount;
        const Timer graphPrologueRecordingBegin = TimerNow();
        for(u32 barrierIndex = 0u; recorded && barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = prologueBarriers[barrierIndex];
            if(barrier.isGraphInitialState && barrier.before == ResourceStates::Unknown){
                if(!__hidden_gpu_packet_runtime_recording::HasExplicitKnownInitialState(declarationAccess, barrier, *commandList)){
                    const GpuTaskGraphResourceView resource = declarationAccess.resourceAt(barrier.resource.index);
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
                recorded = graph.applyCompiledBarrier(
                    compiledGraph,
                    planAccess,
                    recordingAccess,
                    task,
                    loweredBarrier,
                    *commandList
                );
            }
            else
                recorded = graph.applyCompiledBarrier(
                    compiledGraph,
                    planAccess,
                    recordingAccess,
                    task,
                    barrier,
                    *commandList
                );
        }
        // A retained state that already matches the compiler plan still needs a native tracker entry. Otherwise a
        // later packet cannot import that graph-declared resource state, and a renderer thunk would need a redundant
        // direct transition merely to publish its handoff.
        if(recorded)
            recorded = graph.seedTaskRetainedResourceStates(
                compiledGraph,
                planAccess,
                recordingAccess,
                task,
                *commandList
            );
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
        CommandMarkerRecordingToken taskMarker;
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
                taskMarker = commandList->beginMarkerLease(taskView.markerLabel);
            }
            taskRecordingLeaseIntact = commandList->matchesRecordingLease(taskRecordingLeaseSerial);
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
                if(recorded){
                    taskTiming.value().finishTiming(*commandList);
                    if(!taskTiming.value().finishMarker())
                        recorded = false;
                }
                else{
                    taskTiming.value().discardTiming();
                    taskTiming.value().abandonMarker();
                }
            }
            else{
                taskTiming.value().discardTiming();
                taskTiming.value().abandonMarker();
            }
            taskTiming.reset();
        }
        else if(taskMarker.valid()){
            if(recorded && taskRecordingLeaseIntact)
                recorded = commandList->endMarkerLease(taskMarker);
            else
                commandList->abandonMarkerLease(taskMarker);
        }
        const GpuCompiledBarrier* const epilogueBarriers = compiledTaskView.epilogueBarriers;
        const Timer graphEpilogueRecordingBegin = TimerNow();
        for(u32 barrierIndex = 0u; recorded && barrierIndex < compiledTask->epilogueBarrierCount; ++barrierIndex)
            recorded = graph.applyCompiledBarrier(
                compiledGraph,
                planAccess,
                recordingAccess,
                task,
                epilogueBarriers[barrierIndex],
                *commandList
            );
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
            if(recorded){
                packetTiming.value().finishTiming(*commandList);
                if(!packetTiming.value().finishMarker())
                    recorded = false;
            }
            else{
                packetTiming.value().discardTiming();
                packetTiming.value().abandonMarker();
            }
        }
        else{
            packetTiming.value().discardTiming();
            packetTiming.value().abandonMarker();
        }
        packetTiming.reset();
    }
    else if(packetMarker.valid()){
        if(recorded && packetRecordingLeaseIntact)
            recorded = commandList->endMarkerLease(packetMarker);
        else
            commandList->abandonMarkerLease(packetMarker);
    }
    packetTimingRecordingScope.reset();
    if(recorded && commandList->commandRecordingFailed())
        recorded = false;
    recorded = graphRecordingOwnership.finish(recorded, packetStateSeed);
    if(!recorded){
        packetStateSeed->reset();
        abortPacketRecording();
        if(commandIrCapture)
            commandIrCapture->rollback(captureRecordCount);
        return false;
    }
    const Timer recordingEnd = TimerNow();
    GpuRecordedPacket* const recordedPacketStorage = outRecordedGraph.packetStorage(packetID, artifactAccess);
    NWB_FATAL_ASSERT_MSG(recordedPacketStorage, "recorded packet publication requires exact artifact storage");
    if(!recordedPacketStorage)
        TerminateInvariant();
    GpuRecordedPacket& recordedPacket = *recordedPacketStorage;
    recordedPacket.packet = packetID;
    recordedPacket.commandLists[0u] = commandList.get();
    recordedPacket.commandListRecordingLeaseSerials[0u] = packetRecordingLeaseSerial;
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
    graphRecordingOwnership.publish();
    recordedPacket.commandListCount = 1u;
    PacketArtifactPublicationScope artifactPublication(outRecordedGraph, packetID, artifactAccess);
    if(!graph.completePacketRecording(compiledGraph, planAccess, packetID, recordingLease)){
        NWB_FATAL_ASSERT_MSG(false, "recorded artifact publication must retain its exact graph packet claim");
        TerminateInvariant();
    }
    artifactPublication.complete();
    return true;
}

bool GpuNativePacketRecorder::prepareRecordingAttempt(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketRange& range,
    GpuRecordedGraph& outRecordedGraph,
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuRecordedGraph::ArtifactOperation& artifactAccess
)const{
    if(
        !range.valid()
        || !planAccess.validFor(compiledGraph)
        || !artifactAccess.exclusiveFor(outRecordedGraph)
        || !planAccess.validFor(declarationAccess)
        || !declarationAccess.validForDeviceGeneration(planAccess.deviceGeneration())
        || m_device.getDeviceGeneration() != planAccess.deviceGeneration()
        || !planAccess.validPacketRange(range)
    )
        return false;
    Alloc::ScratchArena timingScratchArena(__hidden_gpu_packet_runtime_recording::s_PacketTimingScratchArena);
    if(!__hidden_gpu_packet_runtime_recording::PrepareCompiledTimingQueries(
        declarationAccess,
        planAccess,
        m_timingRecorder,
        timingScratchArena
    ))
        return false;
    const bool artifactMatchesPlan = outRecordedGraph.validForWithinArtifactOperation(
        graph,
        declarationAccess,
        compiledGraph,
        planAccess,
        artifactAccess
    );
    if(artifactMatchesPlan && graph.matchesRecordingAttempt(
            compiledGraph,
            outRecordedGraph.recordingAttemptGenerationWithinArtifactOperation(artifactAccess)
        )){
        GpuTimingRecorder* const artifactTimingRecorder =
            outRecordedGraph.timingRecorderWithinArtifactOperation(artifactAccess)
        ;
        return !artifactTimingRecorder || artifactTimingRecorder == m_timingRecorder;
    }
    if(!outRecordedGraph.prepareRecordingStorageCandidate(compiledGraph, planAccess, m_timingRecorder))
        return false;

    GpuTaskGraph::RecordingAttemptScope provisionalAttempt;
    if(!graph.beginRecordingAttempt(
        compiledGraph,
        range.first,
        declarationAccess,
        planAccess,
        provisionalAttempt
    ))
        return false;

    const usize rangeBegin = range.first.index;
    const usize rangeEnd = rangeBegin + range.packetCount;
    for(usize packetIndex = rangeBegin + 1u; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packetID = planAccess.packetIdAt(packetIndex);
        if(!graph.beginRecordingAttempt(
            compiledGraph,
            packetID,
            declarationAccess,
            planAccess,
            provisionalAttempt
        ))
            return false;
    }

    if(!outRecordedGraph.validForWithinArtifactOperation(
        graph,
        declarationAccess,
        compiledGraph,
        planAccess,
        artifactAccess
    ) && !provisionalAttempt.m_graph)
        return false;
    if(!provisionalAttempt.m_graph)
        return true;

    outRecordedGraph.publishStorageCandidate(
        &graph,
        compiledGraph,
        planAccess,
        provisionalAttempt.m_recordingAttemptGeneration,
        artifactAccess
    );
    provisionalAttempt.complete();
    return outRecordedGraph.validForWithinArtifactOperation(
        graph,
        declarationAccess,
        compiledGraph,
        planAccess,
        artifactAccess
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

