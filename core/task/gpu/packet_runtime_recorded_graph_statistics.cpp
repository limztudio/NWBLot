// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime_recorded_graph.h"

#include "packet_runtime.h"
#include "packet_runtime_internal.h"
#include "task_graph.h"

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_recorded_graph_statistics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_MinOverlapPacketCount = 2u;

template<typename StatisticsT>
static void AccumulatePacketRecordingCounts(StatisticsT& statistics, SIMDVectorDouble& inOutRecordingSeconds, const GpuRecordedPacket& recordedPacket, const bool overlaps)noexcept{
    ++statistics.packetCount;
    statistics.taskCount += recordedPacket.taskCount;
    statistics.commandListCount += recordedPacket.commandListCount;
    statistics.barrierCount += recordedPacket.barrierCount;
    if(recordedPacket.recordingWorkerIndex != 0u)
        ++statistics.workerRoutedPacketCount;
    // Overlap is graph-wide even when accumulating only one physical queue.
    if(overlaps)
        ++statistics.parallelPacketCount;
    inOutRecordingSeconds = VectorDoubleAdd(inOutRecordingSeconds, VectorDoubleSet(recordedPacket.commandListAcquisitionSeconds, recordedPacket.graphBarrierRecordingSeconds, recordedPacket.taskRecordSeconds, recordedPacket.recordingSeconds));
}

template<typename StatisticsT>
static void StorePacketRecordingSeconds(StatisticsT& statistics, const SIMDVectorDouble recordingSeconds)noexcept{
    statistics.commandListAcquisitionSeconds = VectorDoubleGetX(recordingSeconds);
    statistics.graphBarrierRecordingSeconds = VectorDoubleGetY(recordingSeconds);
    statistics.taskRecordSeconds = VectorDoubleGetZ(recordingSeconds);
    statistics.recordingSeconds = VectorDoubleGetW(recordingSeconds);
}

struct PacketRecordingIntervalEntry{
    u64 beginNanoseconds = 0u;
    u64 endNanoseconds = 0u;
    u32 packetIndex = 0u;
};

[[nodiscard]] bool LessPacketRecordingIntervalEntry(const PacketRecordingIntervalEntry& left, const PacketRecordingIntervalEntry& right)noexcept{
    return left.beginNanoseconds < right.beginNanoseconds || (left.beginNanoseconds == right.beginNanoseconds && (left.endNanoseconds < right.endNanoseconds || (left.endNanoseconds == right.endNanoseconds && left.packetIndex < right.packetIndex)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraphRecordingStatistics GpuRecordedGraph::recordingStatistics(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess)const noexcept{
    GpuTaskGraphRecordingStatistics statistics;
    if(!planAccess.validFor(compiledGraph))
        return statistics;
    ArtifactOperation artifactOperation(*this, ArtifactOperationMode::Read);
    if(!artifactOperation.valid())
        return statistics;
    if(!validForWithinArtifactOperation(compiledGraph, planAccess, artifactOperation))
        return statistics;

    const ArtifactStorage& storage = *m_activeStorage;
    statistics.graphGeneration = storage.generation;
    statistics.planGeneration = storage.planGeneration;
    statistics.recordingAttemptGeneration = storage.recordingAttemptGeneration;
    statistics.deviceGeneration = storage.deviceGeneration;
    statistics.recordingElapsedSeconds = storage.recordingElapsedSeconds;
    statistics.readyFrontierElapsedSeconds = storage.readyFrontierElapsedSeconds;
    statistics.readyFrontierWorkerBusySeconds = storage.readyFrontierWorkerBusySeconds;
    statistics.readyFrontierWorkerCapacitySeconds = storage.readyFrontierWorkerCapacitySeconds;
    SIMDVectorDouble recordingSeconds = VectorDoubleZero();
    for(usize packetIndex = 0u; packetIndex < storage.packetCount; ++packetIndex){
        const GpuRecordedPacket& recordedPacket = storage.packets[packetIndex];
        if(recordedPacket.commandListCount == 0u || recordedPacket.packet != planAccess.packetIdAt(packetIndex))
            continue;

        __hidden_gpu_packet_runtime_recorded_graph_statistics::AccumulatePacketRecordingCounts(statistics, recordingSeconds, recordedPacket, storage.packetRecordingOverlaps[packetIndex] != 0u);
    }
    __hidden_gpu_packet_runtime_recorded_graph_statistics::StorePacketRecordingSeconds(statistics, recordingSeconds);
    return statistics;
}

GpuTaskGraphPhysicalQueueRecordingStatistics GpuRecordedGraph::physicalQueueRecordingStatistics(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuPhysicalQueueId& queue)const noexcept{
    if(!planAccess.validFor(compiledGraph))
        return {};
    ArtifactOperation artifactOperation(*this, ArtifactOperationMode::Read);
    if(!artifactOperation.valid())
        return {};
    if(!validForWithinArtifactOperation(compiledGraph, planAccess, artifactOperation))
        return {};

    const ArtifactStorage& storage = *m_activeStorage;
    const GpuPhysicalQueueInfo* const queueInfo = planAccess.queueInfo(queue);
    if(!queueInfo || queueInfo->queueClass >= CommandQueue::Count)
        return {};

    GpuTaskGraphPhysicalQueueRecordingStatistics statistics{
        .graphGeneration = storage.generation,
        .planGeneration = storage.planGeneration,
        .recordingAttemptGeneration = storage.recordingAttemptGeneration,
        .queue = queue,
        .deviceGeneration = storage.deviceGeneration,
        .queueClass = queueInfo->queueClass,
    };
    SIMDVectorDouble recordingSeconds = VectorDoubleZero();
    for(usize packetIndex = 0u; packetIndex < storage.packetCount; ++packetIndex){
        const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
        const GpuRecordedPacket& recordedPacket = storage.packets[packetIndex];
        if(recordedPacket.commandListCount == 0u || recordedPacket.packet != packet)
            continue;

        const GpuCompiledPacketView packetView = planAccess.packet(packet);
        if(!packetView.valid())
            return {};
        const GpuSubmissionPacket& packetPlan = *packetView.plan;
        if(packetPlan.queue != queue)
            continue;

        __hidden_gpu_packet_runtime_recorded_graph_statistics::AccumulatePacketRecordingCounts(statistics, recordingSeconds, recordedPacket, storage.packetRecordingOverlaps[packetIndex] != 0u);
    }
    __hidden_gpu_packet_runtime_recorded_graph_statistics::StorePacketRecordingSeconds(statistics, recordingSeconds);
    return statistics;
}

void GpuRecordedGraph::addRecordingElapsedSeconds(const f64 elapsedSeconds, const ArtifactOperation& artifactAccess)noexcept{
    if(artifactAccess.exclusiveFor(*this) && m_activeStorage && m_activeStorage->valid)
        m_activeStorage->recordingElapsedSeconds = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(m_activeStorage->recordingElapsedSeconds), VectorDoubleReplicate(elapsedSeconds)));
}

void GpuRecordedGraph::addReadyFrontierStatistics(const f64 elapsedSeconds, const f64 workerBusySeconds, const f64 workerCapacitySeconds, const ArtifactOperation& artifactAccess)noexcept{
    if(!artifactAccess.exclusiveFor(*this) || !m_activeStorage || !m_activeStorage->valid)
        return;
    const SIMDVectorDouble previous = VectorDoubleSet(m_activeStorage->readyFrontierElapsedSeconds, m_activeStorage->readyFrontierWorkerBusySeconds, m_activeStorage->readyFrontierWorkerCapacitySeconds, 0.0);
    const SIMDVectorDouble total = VectorDoubleAdd(previous, VectorDoubleSet(elapsedSeconds, workerBusySeconds, workerCapacitySeconds, 0.0));
    m_activeStorage->readyFrontierElapsedSeconds = VectorDoubleGetX(total);
    m_activeStorage->readyFrontierWorkerBusySeconds = VectorDoubleGetY(total);
    m_activeStorage->readyFrontierWorkerCapacitySeconds = VectorDoubleGetZ(total);
}

void GpuRecordedGraph::cachePacketRecordingOverlaps(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const Span<const u32> packetIndices,
    Alloc::ScratchArena& scratchArena,
    const ArtifactOperation& artifactAccess
){
    using IntervalEntry = __hidden_gpu_packet_runtime_recorded_graph_statistics::PacketRecordingIntervalEntry;
    const bool artifactAccessValid = planAccess.validFor(compiledGraph) && artifactAccess.exclusiveFor(*this) && m_activeStorage && m_activeStorage->valid;
    NWB_FATAL_ASSERT_MSG(artifactAccessValid, "Packet overlap caching requires its exact valid artifact writer");
    if(packetIndices.size() < __hidden_gpu_packet_runtime_recorded_graph_statistics::s_MinOverlapPacketCount)
        return;
    ArtifactStorage& storage = *m_activeStorage;

    Vector<IntervalEntry, Alloc::ScratchArena> intervalEntries(scratchArena);
    intervalEntries.reserve(packetIndices.size());
    for(const u32 packetIndex : packetIndices){
        if(packetIndex >= storage.packetCount)
            continue;
        const GpuRecordedPacket& recordedPacket = storage.packets[packetIndex];
        if(recordedPacket.commandListCount == 0u || recordedPacket.packet != planAccess.packetIdAt(packetIndex) || recordedPacket.recordingBeginNanoseconds >= recordedPacket.recordingEndNanoseconds)
            continue;
        intervalEntries.push_back(IntervalEntry{ .beginNanoseconds = recordedPacket.recordingBeginNanoseconds, .endNanoseconds = recordedPacket.recordingEndNanoseconds, .packetIndex = packetIndex, });
    }
    Sort(intervalEntries.begin(), intervalEntries.end(), __hidden_gpu_packet_runtime_recorded_graph_statistics::LessPacketRecordingIntervalEntry);

    u64 maximumPreviousEndNanoseconds = 0u;
    for(usize intervalIndex = 0u; intervalIndex < intervalEntries.size(); ++intervalIndex){
        const IntervalEntry& interval = intervalEntries[intervalIndex];
        const bool overlapsPrevious = intervalIndex != 0u && interval.beginNanoseconds < maximumPreviousEndNanoseconds;
        const bool overlapsNext = intervalIndex + 1u < intervalEntries.size() && intervalEntries[intervalIndex + 1u].beginNanoseconds < interval.endNanoseconds;
        if(overlapsPrevious || overlapsNext)
            storage.packetRecordingOverlaps[interval.packetIndex] = 1u;
        maximumPreviousEndNanoseconds = Max(maximumPreviousEndNanoseconds, interval.endNanoseconds);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

