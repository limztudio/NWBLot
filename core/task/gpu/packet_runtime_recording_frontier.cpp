// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime_internal.h"
#include "task_graph.h"

#include <core/graphics/backend_selection/backend.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuNativePacketRecorder::ReadyFrontierRecordingUnwindScope final : NoCopy{
public:
    ReadyFrontierRecordingUnwindScope(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuRecordedGraph& recordedGraph,
        const GpuRecordedGraph::ArtifactOperation& artifactOperation,
        const Vector<u32, Alloc::ScratchArena>& packetIndices,
        Vector<GpuPacketRecordingAbort, Alloc::ScratchArena>& aborts,
        const u64 recordingAttemptGeneration
    )noexcept
        : m_graph(graph)
        , m_compiledGraph(compiledGraph)
        , m_planAccess(planAccess)
        , m_recordedGraph(recordedGraph)
        , m_artifactOperation(artifactOperation)
        , m_packetIndices(packetIndices)
        , m_aborts(aborts)
        , m_recordingAttemptGeneration(recordingAttemptGeneration)
    {}

    ~ReadyFrontierRecordingUnwindScope()noexcept{
        if(!m_active)
            return;

        for(usize parallelIndex = 0u; parallelIndex < m_aborts.size(); ++parallelIndex){
            GpuPacketRecordingAbort& abort = m_aborts[parallelIndex];
            if(!abort.valid())
                continue;
            const GpuSubmissionPacketId packet = m_planAccess.packetIdAt(m_packetIndices[parallelIndex]);
            m_recordedGraph.abandonPacketTimingTicketWithoutCallbacks(packet, m_artifactOperation);
            const bool abortAbandoned = m_graph.abandonPacketRecordingAbortWithoutCallbacks(m_compiledGraph, m_planAccess, abort);
            NWB_FATAL_ASSERT_MSG(abortAbandoned, "joined packet unwind must consume every deferred recording abort");
        }
        const bool recordingAttemptResolved = m_graph.resolveRecordingAttemptIfTerminal(m_compiledGraph, m_recordingAttemptGeneration);
        NWB_FATAL_ASSERT_MSG(recordingAttemptResolved, "joined packet unwind must preserve or resolve its exact recording-plan lease");
    }


public:
    void release()noexcept{ m_active = false; }


private:
    const GpuTaskGraph& m_graph;
    const GpuCompiledGraph& m_compiledGraph;
    const GpuCompiledGraph::ReadView& m_planAccess;
    GpuRecordedGraph& m_recordedGraph;
    const GpuRecordedGraph::ArtifactOperation& m_artifactOperation;
    const Vector<u32, Alloc::ScratchArena>& m_packetIndices;
    Vector<GpuPacketRecordingAbort, Alloc::ScratchArena>& m_aborts;
    u64 m_recordingAttemptGeneration = 0u;
    bool m_active = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_recording_frontier{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_MinParallelRecordingPacketCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PacketRecordingFrontierEntry{
    GpuSubmissionPacketId packet;
    u32 frontier = 0u;
    bool allowsParallelRecording = false;
};

[[nodiscard]] bool LessPacketRecordingFrontierEntry(const PacketRecordingFrontierEntry& left, const PacketRecordingFrontierEntry& right)noexcept{
    return left.frontier < right.frontier || (left.frontier == right.frontier && left.packet.index < right.packet.index);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuNativePacketRecorder::recordPacketRangeInCompileOrder(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketRange& range,
    GpuRecordedGraph& outRecordedGraph,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket,
    GpuCommandIrCapture* const commandIrCapture
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    GpuRecordedGraph::ArtifactOperation artifactOperation(outRecordedGraph, GpuRecordedGraph::ArtifactOperationMode::Exclusive);
    if(!artifactOperation.valid())
        return false;
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;
    if(!range.valid() || !planAccess.validPacketRange(range))
        return false;

    const Timer recordingOperationBegin = TimerNow();
    {
        GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::TryAcquire(graph);
        if(!declarationAccess.valid() || !prepareRecordingAttempt(graph, compiledGraph, range, outRecordedGraph, declarationAccess, planAccess, artifactOperation, scratchArena))
            return false;
    }
    GpuRecordedGraph::PacketRecordingScratch* const serialScratch = outRecordedGraph.serialRecordingScratch(artifactOperation);
    if(!serialScratch || !serialScratch->stateFanInScratchArena)
        return false;
    Alloc::ScratchArena& stateFanInScratchArena = *serialScratch->stateFanInScratchArena;
    if(!recordPreparedPacketRangeInCompileOrder(graph, compiledGraph, planAccess, artifactOperation, range, outRecordedGraph, *serialScratch, stateFanInScratchArena, commandIrCapture, outFailedPacket))
        return false;
    outRecordedGraph.addRecordingElapsedSeconds(DurationInSeconds<f64>(TimerNow(), recordingOperationBegin), artifactOperation);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuNativePacketRecorder::recordTaskRangeInCompileOrder(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskId firstTask,
    const GpuTaskId lastTask,
    GpuRecordedGraph& outRecordedGraph,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket,
    GpuCommandIrCapture* const commandIrCapture
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    GpuSubmissionPacketRange range;
    {
        GpuCompiledGraph::ReadView planAccess(compiledGraph);
        if(!planAccess.valid())
            return false;
        range = planAccess.packetRangeForTasks(firstTask, lastTask);
    }
    return recordPacketRangeInCompileOrder(graph, compiledGraph, range, outRecordedGraph, scratchArena, outFailedPacket, commandIrCapture);
}


bool GpuNativePacketRecorder::recordPacketRangeInReadyFrontiers(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketRange& range,
    GpuRecordedGraph& outRecordedGraph,
    CpuTaskScheduler& cpuScheduler,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket,
    GpuCommandIrCapture* const commandIrCapture
)const{
    using RecordingEntry = __hidden_gpu_packet_runtime_recording_frontier::PacketRecordingFrontierEntry;

    if(outFailedPacket)
        *outFailedPacket = {};
    GpuRecordedGraph::ArtifactOperation artifactOperation(outRecordedGraph, GpuRecordedGraph::ArtifactOperationMode::Exclusive);
    if(!artifactOperation.valid())
        return false;
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;
    if(!range.valid() || !planAccess.validPacketRange(range))
        return false;

    const usize rangeBegin = range.first.index;
    const usize rangeEnd = rangeBegin + range.packetCount;
    const Timer recordingOperationBegin = TimerNow();
    Vector<RecordingEntry, Alloc::ScratchArena> recordingEntries(scratchArena);
    recordingEntries.reserve(range.packetCount);
    Vector<u32, Alloc::ScratchArena> parallelPacketIndices(scratchArena);
    Vector<u8, Alloc::ScratchArena> parallelResults(scratchArena);
    Vector<GpuPacketRecordingAbort, Alloc::ScratchArena> parallelAborts(scratchArena);
    usize parallelPacketCount = 0u;
    const bool usesParallelRecording = !commandIrCapture && cpuScheduler.isParallelEnabled() && range.packetCount >= __hidden_gpu_packet_runtime_recording_frontier::s_MinParallelRecordingPacketCount;
    bool recordingFrontiersAreMonotonic = true;
    {
        GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::TryAcquire(graph);
        if(!declarationAccess.valid())
            return false;

        for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
            const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
            const GpuCompiledPacketView packetView = planAccess.packet(packet);
            if(!packetView.valid())
                return false;

            u32 effectiveFrontier = packetView.plan->recordingFrontier;
            if(effectiveFrontier == Limit<u32>::s_Max)
                return false;
            const auto raiseFromSourcePacket = [&](const GpuSubmissionPacketId sourcePacket)noexcept{
                if(!planAccess.validPacket(sourcePacket) || sourcePacket == packet || sourcePacket.index >= packet.index)
                    return false;
                if(sourcePacket.index < rangeBegin)
                    return true;

                const usize sourceDescIndex = sourcePacket.index - rangeBegin;
                if(sourceDescIndex >= recordingEntries.size())
                    return false;
                const u32 sourceFrontier = recordingEntries[sourceDescIndex].frontier;
                if(sourceFrontier >= Limit<u32>::s_Max - 1u)
                    return false;
                effectiveFrontier = Max(effectiveFrontier, sourceFrontier + 1u);
                return true;
            };

            const GpuSubmissionPacket& packetPlan = *packetView.plan;
            const GpuTaskId* const tasks = packetView.tasks;
            if(packetPlan.taskCount == 0u)
                return false;
            bool allowsParallelRecording = true;
            for(u32 taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
                const GpuTaskGraphTaskView task = declarationAccess.taskAt(tasks[taskIndex].index);
                const GpuCompiledTaskView compiledTaskView = planAccess.findTask(tasks[taskIndex]);
                const GpuCompiledTask* const compiledTask = compiledTaskView.plan;
                const GpuPacketStateSeed* const stateSeeds = compiledTaskView.prologueStateSeeds;
                if(task.id != tasks[taskIndex] || !compiledTaskView.valid())
                    return false;
                allowsParallelRecording = allowsParallelRecording && task.scheduling.allowParallelRecording;
                for(u32 seedIndex = 0u; seedIndex < compiledTask->prologueStateSeedCount; ++seedIndex){
                    if(!raiseFromSourcePacket(stateSeeds[seedIndex].sourcePacket))
                        return false;
                }
            }
            if(!recordingEntries.empty() && effectiveFrontier < recordingEntries.back().frontier)
                recordingFrontiersAreMonotonic = false;
            recordingEntries.push_back(RecordingEntry{ .packet = packet, .frontier = effectiveFrontier, .allowsParallelRecording = allowsParallelRecording, });
            if(allowsParallelRecording)
                ++parallelPacketCount;
        }
        if(usesParallelRecording){
            parallelPacketIndices.reserve(parallelPacketCount);
            parallelResults.reserve(parallelPacketCount);
            parallelAborts.reserve(parallelPacketCount);
        }
        if(!prepareRecordingAttempt(graph, compiledGraph, range, outRecordedGraph, declarationAccess, planAccess, artifactOperation, scratchArena))
            return false;
    }
    GpuRecordedGraph::PacketRecordingScratch* const serialScratch = outRecordedGraph.serialRecordingScratch(artifactOperation);
    if(!serialScratch || !serialScratch->stateFanInScratchArena)
        return false;
    Alloc::ScratchArena& serialStateFanInScratchArena = *serialScratch->stateFanInScratchArena;
    const auto completeReadyFrontierTelemetry = [&]()noexcept{
        const f64 elapsedSeconds = DurationInSeconds<f64>(TimerNow(), recordingOperationBegin);
        SIMDVectorDouble workerBusySeconds = VectorDoubleZero();
        for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
            const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
            const GpuRecordedPacket* const recordedPacket = outRecordedGraph.findWithinArtifactOperation(packet, artifactOperation);
            if(recordedPacket)
                workerBusySeconds = VectorDoubleAdd(workerBusySeconds, VectorDoubleReplicate(recordedPacket->recordingSeconds));
        }

        // CpuTaskScheduler workers and its calling thread are all callable logical recording slots.
        // Keeping the entire successful ready-frontier operation in the denominator exposes serial fallbacks and underfilled frontiers.
        const f64 workerCount = static_cast<f64>(cpuScheduler.workerThreadCount());
        const SIMDVectorDouble logicalWorkerSlotCount = VectorDoubleAdd(VectorDoubleReplicate(workerCount), VectorDoubleSplatOne());
        outRecordedGraph.addRecordingElapsedSeconds(elapsedSeconds, artifactOperation);
        const f64 availableWorkerSeconds = VectorDoubleGetX(VectorDoubleMultiply(VectorDoubleReplicate(elapsedSeconds), logicalWorkerSlotCount));
        outRecordedGraph.addReadyFrontierStatistics(elapsedSeconds, VectorDoubleGetX(workerBusySeconds), availableWorkerSeconds, artifactOperation);
    };

    // Command-IR records form one linear artifact. Keep capture serial to preserve record order and the rollback contract; this records right after prepare so the operation owns one span.
    if(!usesParallelRecording){
        if(!recordPreparedPacketRangeInCompileOrder(graph, compiledGraph, planAccess, artifactOperation, range, outRecordedGraph, *serialScratch, serialStateFanInScratchArena, commandIrCapture, outFailedPacket))
            return false;
        completeReadyFrontierTelemetry();
        return true;
    }

    const auto packetStateSeedsAreRecorded = [&](const GpuSubmissionPacketId packet)noexcept{
        const GpuCompiledPacketView packetView = planAccess.packetWithTasks(packet);
        if(!packetView.valid())
            return false;
        const GpuSubmissionPacket& packetPlan = *packetView.plan;
        const GpuTaskId* const tasks = packetView.tasks;

        for(u32 taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskId task = tasks[taskIndex];
            const GpuCompiledTaskView compiledTaskView = planAccess.findTask(task);
            if(!compiledTaskView.valid())
                return false;
            const GpuCompiledTask& compiledTask = *compiledTaskView.plan;
            const GpuPacketStateSeed* const stateSeeds = compiledTaskView.prologueStateSeeds;
            for(u32 stateSeedIndex = 0u; stateSeedIndex < compiledTask.prologueStateSeedCount; ++stateSeedIndex){
                const GpuSubmissionPacketId sourcePacket = stateSeeds[stateSeedIndex].sourcePacket;
                if(!planAccess.validPacket(sourcePacket) || sourcePacket == packet || sourcePacket.index >= packet.index || !outRecordedGraph.findWithinArtifactOperation(sourcePacket, artifactOperation))
                    return false;
            }
        }
        return true;
    };
    // Compiler order is already free for monotonic frontiers, including a deep state-seed chain. A later independent packet may lower the frontier again.
    if(!recordingFrontiersAreMonotonic){
        Sort(recordingEntries.begin(), recordingEntries.end(), __hidden_gpu_packet_runtime_recording_frontier::LessPacketRecordingFrontierEntry);
    }

    usize frontierBegin = 0u;
    while(frontierBegin < recordingEntries.size()){
        usize frontierEnd = frontierBegin + 1u;
        while(frontierEnd < recordingEntries.size() && recordingEntries[frontierEnd].frontier == recordingEntries[frontierBegin].frontier)
            ++frontierEnd;

        parallelPacketIndices.clear();
        for(usize recordingIndex = frontierBegin; recordingIndex < frontierEnd; ++recordingIndex){
            const GpuSubmissionPacketId packet = recordingEntries[recordingIndex].packet;
            if(!packetStateSeedsAreRecorded(packet)){
                if(outFailedPacket)
                    *outFailedPacket = packet;
                return false;
            }
            if(recordingEntries[recordingIndex].allowsParallelRecording)
                parallelPacketIndices.push_back(packet.index);
            else if(!recordPacket(graph, compiledGraph, planAccess, artifactOperation, packet, outRecordedGraph, *serialScratch, serialStateFanInScratchArena, nullptr)){
                if(outFailedPacket)
                    *outFailedPacket = packet;
                return false;
            }
        }

        if(!parallelPacketIndices.empty()){
            parallelResults.resize(parallelPacketIndices.size());
            parallelAborts.resize(parallelPacketIndices.size());
            const u64 recordingAttemptGeneration = outRecordedGraph.recordingAttemptGenerationWithinArtifactOperation(artifactOperation);
            ReadyFrontierRecordingUnwindScope recordingUnwind(graph, compiledGraph, planAccess, outRecordedGraph, artifactOperation, parallelPacketIndices, parallelAborts, recordingAttemptGeneration);

            const auto parallelWork = cpuScheduler.parallelFor(0u, parallelPacketIndices.size(), [&](const usize parallelIndex){
                const GpuSubmissionPacketId packet = planAccess.packetIdAt(parallelPacketIndices[parallelIndex]);
                GpuRecordedGraph::PacketRecordingScratch* const scratch = outRecordedGraph.packetRecordingScratch(packet, artifactOperation);
                if(!scratch || !scratch->stateFanInScratchArena){
                    parallelResults[parallelIndex] = 0u;
                    return;
                }
                // Keep zero for serial/direct lists. The caller is worker zero, so shift every lease by one. The stable domain stops a second scheduler with the same index from aliasing this shard.
                parallelResults[parallelIndex] = recordPacket(
                    graph,
                    compiledGraph,
                    planAccess,
                    artifactOperation,
                    packet,
                    outRecordedGraph,
                    *scratch,
                    *scratch->stateFanInScratchArena,
                    nullptr,
                    cpuScheduler.domainIdentity(),
                    static_cast<u32>(cpuScheduler.currentWorkerIndex() + 1u),
                    &parallelAborts[parallelIndex]
                ) ? 1u : 0u;
            }
            );
            for(usize parallelIndex = 0u; parallelIndex < parallelAborts.size(); ++parallelIndex){
                GpuPacketRecordingAbort& abort = parallelAborts[parallelIndex];
                if(!abort.valid())
                    continue;
                const GpuSubmissionPacketId packet = planAccess.packetIdAt(parallelPacketIndices[parallelIndex]);
                outRecordedGraph.discardPacketTimingTicket(packet, artifactOperation);
                const bool abortCompleted = graph.completePacketRecordingAbort(compiledGraph, planAccess, abort);
                NWB_FATAL_ASSERT_MSG(abortCompleted, "joined packet drain must consume every deferred recording abort");
            }
            const bool recordingAttemptResolved = graph.resolveRecordingAttemptIfTerminal(compiledGraph, recordingAttemptGeneration);
            NWB_FATAL_ASSERT_MSG(recordingAttemptResolved, "joined packet drain must preserve or resolve its exact recording-plan lease");
            recordingUnwind.release();
            if(!parallelWork){
                NWB_LOGGER_ERROR(NWB_TEXT("GpuNativePacketRecorder: packet recording rejected CPU work: {}"), static_cast<u32>(parallelWork.error()));
                if(outFailedPacket)
                    *outFailedPacket = planAccess.packetIdAt(parallelPacketIndices.front());
                return false;
            }
            outRecordedGraph.cachePacketRecordingOverlaps(compiledGraph, planAccess, parallelPacketIndices, scratchArena, artifactOperation);
            for(usize parallelIndex = 0u; parallelIndex < parallelResults.size(); ++parallelIndex){
                if(parallelResults[parallelIndex] != 0u)
                    continue;
                if(outFailedPacket)
                    *outFailedPacket = planAccess.packetIdAt(parallelPacketIndices[parallelIndex]);
                return false;
            }
        }
        parallelAborts.clear();
        frontierBegin = frontierEnd;
    }
    completeReadyFrontierTelemetry();
    return true;
}


bool GpuNativePacketRecorder::recordPreparedPacketRangeInCompileOrder(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuRecordedGraph::ArtifactOperation& artifactAccess,
    const GpuSubmissionPacketRange& range,
    GpuRecordedGraph& outRecordedGraph,
    GpuRecordedGraph::PacketRecordingScratch& scratch,
    Alloc::ScratchArena& stateFanInScratchArena,
    GpuCommandIrCapture* const commandIrCapture,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    const usize rangeBegin = range.first.index;
    const usize rangeEnd = rangeBegin + range.packetCount;
    // Compiler packet IDs are in stable topological order, preserving state-seed dependencies.
    for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
        if(!recordPacket(graph, compiledGraph, planAccess, artifactAccess, packet, outRecordedGraph, scratch, stateFanInScratchArena, commandIrCapture)){
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

