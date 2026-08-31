// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"

#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_recorded_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PacketRecordingIntervalEntry{
    u64 beginNanoseconds = 0u;
    u64 endNanoseconds = 0u;
    u32 packetIndex = 0u;
};

[[nodiscard]] bool LessPacketRecordingIntervalEntry(
    const PacketRecordingIntervalEntry& left,
    const PacketRecordingIntervalEntry& right
)noexcept{
    return left.beginNanoseconds < right.beginNanoseconds
        || (
            left.beginNanoseconds == right.beginNanoseconds
            && (
                left.endNanoseconds < right.endNanoseconds
                || (left.endNanoseconds == right.endNanoseconds && left.packetIndex < right.packetIndex)
            )
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuRecordedGraph::GpuRecordedGraph(GraphicsArena& arena)
    : m_arena(arena)
    , m_packets(arena)
    , m_packetRecordingOverlaps(arena)
    , m_packetTimingTickets(arena)
    , m_packetStateSeeds(arena)
    , m_serialRecordingScratch(arena)
    , m_packetRecordingScratch(arena)
{}
GpuRecordedGraph::~GpuRecordedGraph() = default;


void GpuRecordedGraph::reset(const GpuCompiledGraph& compiledGraph){
    m_packets.clear();
    m_packets.resize(compiledGraph.packetCount());
    m_packetRecordingOverlaps.clear();
    m_packetRecordingOverlaps.resize(compiledGraph.packetCount());
    m_packetTimingTickets.clear();
    m_timingRecorder = nullptr;
    m_packetTimingTickets.resize(compiledGraph.packetCount());
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
    m_recordingElapsedSeconds = 0.0;
    m_readyFrontierElapsedSeconds = 0.0;
    m_readyFrontierWorkerBusySeconds = 0.0;
    m_readyFrontierWorkerCapacitySeconds = 0.0;
    m_generation = compiledGraph.generation();
    m_planGeneration = compiledGraph.planGeneration();
    m_recordingAttemptGeneration = 0u;
    m_deviceGeneration = compiledGraph.deviceGeneration();
    m_valid = compiledGraph.valid();
}

void GpuRecordedGraph::resetForRecording(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph
){
    reset(compiledGraph);
    const u64 recordingAttemptGeneration = graph.recordingAttemptGeneration();
    if(!graph.matchesRecordingAttempt(compiledGraph, recordingAttemptGeneration)){
        m_valid = false;
        return;
    }
    m_recordingAttemptGeneration = recordingAttemptGeneration;
}

bool GpuRecordedGraph::validFor(const GpuCompiledGraph& compiledGraph)const noexcept{
    return m_valid
        && compiledGraph.valid()
        && m_generation == compiledGraph.generation()
        && m_planGeneration == compiledGraph.planGeneration()
        && m_deviceGeneration == compiledGraph.deviceGeneration()
        && m_packets.size() == compiledGraph.packetCount()
        && m_packetRecordingOverlaps.size() == compiledGraph.packetCount()
        && m_packetTimingTickets.size() == compiledGraph.packetCount()
        && m_packetStateSeeds.size() == compiledGraph.packetCount()
        && m_packetRecordingScratch.size() == compiledGraph.packetCount()
    ;
}

bool GpuRecordedGraph::validFor(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph
)const noexcept{
    return validFor(compiledGraph)
        && m_recordingAttemptGeneration != 0u
        && graph.matchesRecordingAttempt(compiledGraph, m_recordingAttemptGeneration)
    ;
}


GpuTaskGraphRecordingStatistics GpuRecordedGraph::recordingStatistics(
    const GpuCompiledGraph& compiledGraph
)const noexcept{
    GpuTaskGraphRecordingStatistics statistics;
    if(!validFor(compiledGraph))
        return statistics;

    statistics.graphGeneration = m_generation;
    statistics.planGeneration = m_planGeneration;
    statistics.recordingAttemptGeneration = m_recordingAttemptGeneration;
    statistics.deviceGeneration = m_deviceGeneration;
    statistics.recordingElapsedSeconds = m_recordingElapsedSeconds;
    statistics.readyFrontierElapsedSeconds = m_readyFrontierElapsedSeconds;
    statistics.readyFrontierWorkerBusySeconds = m_readyFrontierWorkerBusySeconds;
    statistics.readyFrontierWorkerCapacitySeconds = m_readyFrontierWorkerCapacitySeconds;
    for(usize packetIndex = 0u; packetIndex < m_packets.size(); ++packetIndex){
        const GpuRecordedPacket& recordedPacket = m_packets[packetIndex];
        if(
            recordedPacket.commandListCount == 0u
            || recordedPacket.packet != compiledGraph.packetIdAt(packetIndex)
        )
            continue;

        ++statistics.packetCount;
        statistics.taskCount += recordedPacket.taskCount;
        statistics.commandListCount += recordedPacket.commandListCount;
        statistics.barrierCount += recordedPacket.barrierCount;
        statistics.commandListAcquisitionSeconds += recordedPacket.commandListAcquisitionSeconds;
        statistics.graphBarrierRecordingSeconds += recordedPacket.graphBarrierRecordingSeconds;
        statistics.taskRecordSeconds += recordedPacket.taskRecordSeconds;
        statistics.recordingSeconds += recordedPacket.recordingSeconds;
        if(recordedPacket.recordingWorkerIndex != 0u)
            ++statistics.workerRoutedPacketCount;
        if(m_packetRecordingOverlaps[packetIndex] != 0u)
            ++statistics.parallelPacketCount;
    }
    return statistics;
}

GpuTaskGraphPhysicalQueueRecordingStatistics GpuRecordedGraph::physicalQueueRecordingStatistics(
    const GpuCompiledGraph& compiledGraph,
    const GpuPhysicalQueueId& queue
)const noexcept{
    if(!validFor(compiledGraph))
        return {};

    const GpuPhysicalQueueInfo* const queueInfo = compiledGraph.queueInfo(queue);
    if(!queueInfo || queueInfo->queueClass >= CommandQueue::kCount)
        return {};

    GpuTaskGraphPhysicalQueueRecordingStatistics statistics{
        .graphGeneration = m_generation,
        .planGeneration = m_planGeneration,
        .recordingAttemptGeneration = m_recordingAttemptGeneration,
        .deviceGeneration = m_deviceGeneration,
        .queue = queue,
        .queueClass = queueInfo->queueClass,
    };
    for(usize packetIndex = 0u; packetIndex < m_packets.size(); ++packetIndex){
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
        const GpuRecordedPacket& recordedPacket = m_packets[packetIndex];
        if(
            recordedPacket.commandListCount == 0u
            || recordedPacket.packet != packet
        )
            continue;

        const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
        if(packetPlan.queue != queue)
            continue;

        ++statistics.packetCount;
        statistics.taskCount += recordedPacket.taskCount;
        statistics.commandListCount += recordedPacket.commandListCount;
        statistics.barrierCount += recordedPacket.barrierCount;
        statistics.commandListAcquisitionSeconds += recordedPacket.commandListAcquisitionSeconds;
        statistics.graphBarrierRecordingSeconds += recordedPacket.graphBarrierRecordingSeconds;
        statistics.taskRecordSeconds += recordedPacket.taskRecordSeconds;
        statistics.recordingSeconds += recordedPacket.recordingSeconds;
        if(recordedPacket.recordingWorkerIndex != 0u)
            ++statistics.workerRoutedPacketCount;
        // The cached flag is graph-wide. A packet on this queue retains overlap with a published packet on another
        // physical queue, preserving the aggregate's cross-queue recording semantics.
        if(m_packetRecordingOverlaps[packetIndex] != 0u)
            ++statistics.parallelPacketCount;
    }
    return statistics;
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

    // A metadata-only compatibility binding may originate on a current-device queue omitted from the assignment topology.
    // Graph-owned tokens instead require complete-topology validation during compile; this fallback validates device
    // lifetime here and leaves concrete queue validation to the submitting Device.
    return token.deviceGeneration == compiledGraph.deviceGeneration();
}

bool GpuTaskGraphExternalCompletionToken::validFallbackFor(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph
)const noexcept{
    return compiledGraph.validFor(graph)
        && graph.validExternalCompletion(completion)
        && !graph.externalCompletionToken(completion)
        && validFor(compiledGraph)
    ;
}

bool GpuTaskGraphExternalResourceHandoff::validFor(const GpuCompiledGraph& compiledGraph)const noexcept{
    if(
        !compiledGraph.valid()
        || planGeneration != compiledGraph.planGeneration()
        || !resource.valid()
        || resource.generation != compiledGraph.generation()
        || !destinationQueue.valid()
        || destinationQueue.deviceGeneration != compiledGraph.deviceGeneration()
        || !compiledGraph.queueInfo(destinationQueue)
        || finalState == ResourceStates::Unknown
        || terminalRangeCount == 0u
        || producerCount == 0u
        || !producers
        || waitTokenCount == 0u
        || !waitTokens
        || !stateSource
        || !stateSource->validForDeviceGeneration(compiledGraph.deviceGeneration())
        || !terminalRanges
    )
        return false;

    for(usize producerIndex = 0u; producerIndex < producerCount; ++producerIndex){
        const GpuTaskGraphExternalResourceHandoffProducer& producer = producers[producerIndex];
        const GpuCompiledTask* const compiledProducer = compiledGraph.findTask(producer.producerTask);
        const GpuPhysicalQueueInfo* const sourceQueueInfo = compiledGraph.queueInfo(producer.sourceQueue);
        if(
            !producer.producerTask.valid()
            || producer.producerTask.generation != compiledGraph.generation()
            || !compiledProducer
            || compiledProducer->queue != producer.sourceQueue
            || !producer.sourceQueue.valid()
            || producer.sourceQueue.deviceGeneration != compiledGraph.deviceGeneration()
            || !sourceQueueInfo
            || !producer.token.valid()
            || producer.token.queue != sourceQueueInfo->queueClass
            || !producer.token.matchesPhysicalQueue(
                producer.sourceQueue.index,
                producer.sourceQueue.deviceGeneration
            )
        )
            return false;

        bool coveredByWait = false;
        for(usize waitIndex = 0u; waitIndex < waitTokenCount; ++waitIndex){
            const QueueSubmissionToken& wait = waitTokens[waitIndex];
            if(
                !wait.valid()
                || !wait.hasPhysicalQueueIdentity()
                || wait.deviceGeneration != compiledGraph.deviceGeneration()
            )
                return false;
            coveredByWait = coveredByWait || (
                wait.queue == producer.token.queue
                && wait.physicalQueueIndex == producer.token.physicalQueueIndex
                && wait.deviceGeneration == producer.token.deviceGeneration
                && wait.value >= producer.token.value
            );
        }
        if(!coveredByWait)
            return false;
    }

    for(usize rangeIndex = 0u; rangeIndex < terminalRangeCount; ++rangeIndex){
        const GpuTaskGraphExternalResourceHandoffRange& range = terminalRanges[rangeIndex];
        bool matchesProducer = false;
        for(usize producerIndex = 0u; producerIndex < producerCount; ++producerIndex){
            const GpuTaskGraphExternalResourceHandoffProducer& producer = producers[producerIndex];
            matchesProducer = matchesProducer || (
                compiledGraph.packetForTask(range.producerTask)
                    == compiledGraph.packetForTask(producer.producerTask)
                && range.sourceQueue == producer.sourceQueue
                && range.token.queue == producer.token.queue
                && range.token.value == producer.token.value
                && range.token.physicalQueueIndex == producer.token.physicalQueueIndex
                && range.token.deviceGeneration == producer.token.deviceGeneration
            );
        }
        if(!matchesProducer)
            return false;
    }

    if(producerCount != 1u)
        return !producerTask.valid() && !sourceQueue.valid() && !token.valid();

    const GpuTaskGraphExternalResourceHandoffProducer& producer = producers[0u];
    return producerTask == producer.producerTask
        && sourceQueue == producer.sourceQueue
        && token.queue == producer.token.queue
        && token.value == producer.token.value
        && token.physicalQueueIndex == producer.token.physicalQueueIndex
        && token.deviceGeneration == producer.token.deviceGeneration
    ;
}

const GpuRecordedPacket* GpuRecordedGraph::find(const GpuSubmissionPacketId& packet)const noexcept{
    if(
        !packet.valid()
        || packet.generation != m_planGeneration
        || packet.index >= m_packets.size()
    )
        return nullptr;
    const GpuRecordedPacket& recordedPacket = m_packets[packet.index];
    return recordedPacket.packet == packet && recordedPacket.commandListCount != 0u
        ? &recordedPacket
        : nullptr
    ;
}

const CommandListResourceStateHandoff* GpuRecordedGraph::taskFinalStateSeed(
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskId task
)const noexcept{
    if(!validFor(compiledGraph) || !compiledGraph.findTask(task))
        return nullptr;
    const GpuSubmissionPacketId packet = compiledGraph.packetForTask(task);
    const CommandListResourceStateHandoff* const stateSeed = packetStateSeed(packet);
    return find(packet) && stateSeed && stateSeed->validForDeviceGeneration(m_deviceGeneration)
        ? stateSeed
        : nullptr
    ;
}

bool GpuRecordedGraph::prepareTimingTickets(
    const GpuCompiledGraph& compiledGraph,
    GpuTimingRecorder& recorder
){
    if(!validFor(compiledGraph))
        return false;

    bool recordsTiming = false;
    for(usize packetIndex = 0u; packetIndex < compiledGraph.packetCount(); ++packetIndex){
        recordsTiming = recordsTiming || compiledGraph.packet(compiledGraph.packetIdAt(packetIndex)).recordsTiming;
    }
    if(recordsTiming && m_timingRecorder && m_timingRecorder != &recorder)
        return false;
    if(recordsTiming)
        m_timingRecorder = &recorder;

    for(usize packetIndex = 0u; packetIndex < compiledGraph.packetCount(); ++packetIndex){
        GlobalUniquePtr<GpuTimingSubmissionTicket>& ticket = m_packetTimingTickets[packetIndex];
        const GpuSubmissionPacket& packet = compiledGraph.packet(compiledGraph.packetIdAt(packetIndex));
        if(!packet.recordsTiming){
            if(ticket)
                return false;
            continue;
        }
        if(!ticket)
            ticket = MakeGlobalUnique<GpuTimingSubmissionTicket>(m_arena, recorder);
        if(!ticket)
            return false;
    }
    return true;
}

void GpuRecordedGraph::discardPacketTimingTicket(const GpuSubmissionPacketId& packet){
    GpuTimingSubmissionTicket* const ticket = packetTimingTicket(packet);
    if(!ticket)
        return;
    ticket->discard();
    m_packetTimingTickets[packet.index].reset();
}

GpuTimingSubmissionTicket* GpuRecordedGraph::packetTimingTicket(
    const GpuSubmissionPacketId& packet
)const noexcept{
    if(
        !packet.valid()
        || packet.generation != m_planGeneration
        || packet.index >= m_packetTimingTickets.size()
    )
        return nullptr;
    return m_packetTimingTickets[packet.index].get();
}

CommandListResourceStateHandoff* GpuRecordedGraph::packetStateSeed(const GpuSubmissionPacketId& packet)noexcept{
    if(!packet.valid() || packet.generation != m_planGeneration || packet.index >= m_packetStateSeeds.size())
        return nullptr;
    return &m_packetStateSeeds[packet.index];
}

const CommandListResourceStateHandoff* GpuRecordedGraph::packetStateSeed(
    const GpuSubmissionPacketId& packet
)const noexcept{
    if(!packet.valid() || packet.generation != m_planGeneration || packet.index >= m_packetStateSeeds.size())
        return nullptr;
    return &m_packetStateSeeds[packet.index];
}

GpuRecordedGraph::PacketRecordingScratch* GpuRecordedGraph::packetRecordingScratch(
    const GpuSubmissionPacketId& packet
)noexcept{
    if(!packet.valid() || packet.generation != m_planGeneration || packet.index >= m_packetRecordingScratch.size())
        return nullptr;
    return &m_packetRecordingScratch[packet.index];
}

void GpuRecordedGraph::cachePacketRecordingOverlaps(
    const GpuCompiledGraph& compiledGraph,
    const Vector<u32, Alloc::ScratchArena>& packetIndices,
    Alloc::ScratchArena& scratchArena
){
    using IntervalEntry = __hidden_gpu_packet_runtime_recorded_graph::PacketRecordingIntervalEntry;

    Vector<IntervalEntry, Alloc::ScratchArena> intervalEntries(scratchArena);
    intervalEntries.reserve(packetIndices.size());
    for(const u32 packetIndex : packetIndices){
        if(
            packetIndex >= m_packets.size()
            || packetIndex >= m_packetRecordingOverlaps.size()
        )
            continue;
        const GpuRecordedPacket& recordedPacket = m_packets[packetIndex];
        if(
            recordedPacket.commandListCount == 0u
            || recordedPacket.packet != compiledGraph.packetIdAt(packetIndex)
            || recordedPacket.recordingBeginNanoseconds >= recordedPacket.recordingEndNanoseconds
        )
            continue;
        intervalEntries.push_back(IntervalEntry{
            .beginNanoseconds = recordedPacket.recordingBeginNanoseconds,
            .endNanoseconds = recordedPacket.recordingEndNanoseconds,
            .packetIndex = packetIndex,
        });
    }
    Sort(
        intervalEntries.begin(),
        intervalEntries.end(),
        __hidden_gpu_packet_runtime_recorded_graph::LessPacketRecordingIntervalEntry
    );

    u64 maximumPreviousEndNanoseconds = 0u;
    for(usize intervalIndex = 0u; intervalIndex < intervalEntries.size(); ++intervalIndex){
        const IntervalEntry& interval = intervalEntries[intervalIndex];
        const bool overlapsPrevious = intervalIndex != 0u
            && interval.beginNanoseconds < maximumPreviousEndNanoseconds
        ;
        const bool overlapsNext = intervalIndex + 1u < intervalEntries.size()
            && intervalEntries[intervalIndex + 1u].beginNanoseconds < interval.endNanoseconds
        ;
        if(overlapsPrevious || overlapsNext)
            m_packetRecordingOverlaps[interval.packetIndex] = 1u;
        maximumPreviousEndNanoseconds = Max(maximumPreviousEndNanoseconds, interval.endNanoseconds);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

