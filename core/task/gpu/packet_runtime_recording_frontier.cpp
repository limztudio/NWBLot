// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime_internal.h"

#include <global/simd/vector_arithmetic.h>
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
        const GpuRecordedGraph::ArtifactOperation& artifactOperation,
        const Span<const u32> packetIndices,
        const u64 recordingAttemptGeneration,
        GpuRecordedGraph& recordedGraph,
        const Span<GpuPacketRecordingAbort> aborts,
        const u32* const serialAbortPacketIndex = nullptr,
        GpuPacketRecordingAbort* const serialAbort = nullptr
    )noexcept
        : m_graph(graph)
        , m_compiledGraph(compiledGraph)
        , m_planAccess(planAccess)
        , m_recordedGraph(recordedGraph)
        , m_artifactOperation(artifactOperation)
        , m_packetIndices(packetIndices)
        , m_aborts(aborts)
        , m_serialAbortPacketIndex(serialAbortPacketIndex)
        , m_serialAbort(serialAbort)
        , m_recordingAttemptGeneration(recordingAttemptGeneration)
    {}
    ~ReadyFrontierRecordingUnwindScope()noexcept{
        if(!m_active)
            return;

        if(m_serialAbort && m_serialAbort->valid()){
            FOUNDATION_ASSERT(m_serialAbortPacketIndex && *m_serialAbortPacketIndex != Limit<u32>::s_Max);
            const GpuSubmissionPacketId packet = m_planAccess.packetIdAt(*m_serialAbortPacketIndex);
            m_recordedGraph.abandonPacketTimingTicketWithoutCallbacks(packet, m_artifactOperation);
            const b8 abortAbandoned = m_graph.abandonPacketRecordingAbortWithoutCallbacks(m_compiledGraph, m_planAccess, *m_serialAbort);
            FOUNDATION_FATAL_ASSERT_MSG(abortAbandoned, "joined packet unwind must consume every deferred serial recording abort");
        }
        for(usize parallelIndex = 0u; parallelIndex < m_aborts.size(); ++parallelIndex){
            GpuPacketRecordingAbort& abort = m_aborts[parallelIndex];
            if(!abort.valid())
                continue;
            const GpuSubmissionPacketId packet = m_planAccess.packetIdAt(m_packetIndices[parallelIndex]);
            m_recordedGraph.abandonPacketTimingTicketWithoutCallbacks(packet, m_artifactOperation);
            const b8 abortAbandoned = m_graph.abandonPacketRecordingAbortWithoutCallbacks(m_compiledGraph, m_planAccess, abort);
            FOUNDATION_FATAL_ASSERT_MSG(abortAbandoned, "joined packet unwind must consume every deferred recording abort");
        }
        const b8 recordingAttemptResolved = m_graph.resolveRecordingAttemptIfTerminal(m_compiledGraph, m_recordingAttemptGeneration);
        FOUNDATION_FATAL_ASSERT_MSG(recordingAttemptResolved, "joined packet unwind must preserve or resolve its exact recording-plan lease");
    }


public:
    void release()noexcept{ m_active = false; }


private:
    const GpuTaskGraph& m_graph;
    const GpuCompiledGraph& m_compiledGraph;
    const GpuCompiledGraph::ReadView& m_planAccess;
    GpuRecordedGraph& m_recordedGraph;
    const GpuRecordedGraph::ArtifactOperation& m_artifactOperation;
    Span<const u32> m_packetIndices;
    Span<GpuPacketRecordingAbort> m_aborts;
    const u32* m_serialAbortPacketIndex = nullptr;
    GpuPacketRecordingAbort* m_serialAbort = nullptr;
    u64 m_recordingAttemptGeneration = 0u;
    b8 m_active = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_recording_frontier{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_MinParallelRecordingPacketCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PacketRecordingFrontierEntry{
    GpuSubmissionPacketId packet;
    u32 frontier = 0u;
    b8 allowsParallelRecording = false;
};

[[nodiscard]] b8 LessPacketRecordingFrontierEntry(const PacketRecordingFrontierEntry& left, const PacketRecordingFrontierEntry& right)noexcept{
    return left.frontier < right.frontier || (left.frontier == right.frontier && left.packet.index < right.packet.index);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuNativePacketRecorder::recordPacketRangeInCompileOrder(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketRange& range,
    Alloc::ScratchArena& scratchArena,
    GpuRecordedGraph& outRecordedGraph,
    GpuSubmissionPacketId* const outFailedPacket,
    GpuCommandIrCapture* const outCommandIrCapture
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
    GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::TryAcquire(graph);
    if(!declarationAccess.valid() || !prepareRecordingAttempt(graph, compiledGraph, range, declarationAccess, planAccess, artifactOperation, scratchArena, outRecordedGraph))
        return false;
    GpuRecordedGraph::PacketRecordingScratch* const serialScratch = outRecordedGraph.serialRecordingScratch(artifactOperation);
    if(!serialScratch || !serialScratch->stateFanInScratchArena)
        return false;
    Alloc::ScratchArena& stateFanInScratchArena = *serialScratch->stateFanInScratchArena;
    if(!recordPreparedPacketRangeInCompileOrder(graph, declarationAccess, compiledGraph, planAccess, artifactOperation, range, stateFanInScratchArena, *serialScratch, outRecordedGraph, outCommandIrCapture, outFailedPacket))
        return false;
    outRecordedGraph.addRecordingElapsedSeconds(DurationInSeconds<f64>(TimerNow(), recordingOperationBegin), artifactOperation);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuNativePacketRecorder::recordTaskRangeInCompileOrder(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskId firstTask,
    const GpuTaskId lastTask,
    Alloc::ScratchArena& scratchArena,
    GpuRecordedGraph& outRecordedGraph,
    GpuSubmissionPacketId* const outFailedPacket,
    GpuCommandIrCapture* const outCommandIrCapture
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
    return recordPacketRangeInCompileOrder(graph, compiledGraph, range, scratchArena, outRecordedGraph, outFailedPacket, outCommandIrCapture);
}


b8 GpuNativePacketRecorder::recordPacketRangeInReadyFrontiers(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketRange& range,
    CpuTaskScheduler& cpuScheduler,
    Alloc::ScratchArena& scratchArena,
    GpuRecordedGraph& outRecordedGraph,
    GpuSubmissionPacketId* const outFailedPacket,
    GpuCommandIrCapture* const outCommandIrCapture
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
    Vector<u32, Alloc::ScratchArena> serialPacketIndices(scratchArena);
    Vector<u32, Alloc::ScratchArena> parallelPacketIndices(scratchArena);
    Vector<u8, Alloc::ScratchArena> parallelResults(scratchArena);
    Vector<GpuPacketRecordingAbort, Alloc::ScratchArena> parallelAborts(scratchArena);
    usize parallelPacketCount = 0u;
    const b8 mightUseParallelRecording = !outCommandIrCapture && cpuScheduler.isParallelEnabled() && range.packetCount >= __hidden_gpu_packet_runtime_recording_frontier::s_MinParallelRecordingPacketCount;
    b8 recordingFrontiersAreMonotonic = true;
    GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::TryAcquire(graph);
    if(!declarationAccess.valid())
        return false;

    if(mightUseParallelRecording){
        recordingEntries.reserve(range.packetCount);
        for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
            const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
            const GpuCompiledPacketView packetView = planAccess.packet(packet);
            if(!packetView.valid())
                return false;

            u32 effectiveFrontier = packetView.plan->recordingFrontier;
            if(effectiveFrontier == Limit<u32>::s_Max)
                return false;
            const auto raiseFromSourcePacket = [&](const GpuSubmissionPacketId sourcePacket)noexcept{
                if(!planAccess.validPacket(sourcePacket) || sourcePacket.index >= packet.index)
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
            b8 allowsParallelRecording = true;
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
    }
    const b8 usesParallelRecording = mightUseParallelRecording && parallelPacketCount != 0u;
    if(usesParallelRecording){
        serialPacketIndices.reserve(range.packetCount - parallelPacketCount);
        parallelPacketIndices.reserve(parallelPacketCount);
        parallelResults.reserve(parallelPacketCount);
        parallelAborts.reserve(parallelPacketCount);
    }
    if(!prepareRecordingAttempt(graph, compiledGraph, range, declarationAccess, planAccess, artifactOperation, scratchArena, outRecordedGraph))
        return false;
    GpuRecordedGraph::PacketRecordingScratch* const serialScratch = outRecordedGraph.serialRecordingScratch(artifactOperation);
    if(!serialScratch || !serialScratch->stateFanInScratchArena)
        return false;
    Alloc::ScratchArena& serialStateFanInScratchArena = *serialScratch->stateFanInScratchArena;
    const auto completeReadyFrontierTelemetry = [&]()noexcept{
        const f64 elapsedSeconds = DurationInSeconds<f64>(TimerNow(), recordingOperationBegin);
        f64 workerBusySeconds = 0.0;
        for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
            const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
            const GpuRecordedPacket* const recordedPacket = outRecordedGraph.findWithinArtifactOperation(packet, artifactOperation);
            if(recordedPacket)
                workerBusySeconds = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(workerBusySeconds), VectorDoubleReplicate(recordedPacket->recordingSeconds)));
        }

        const f64 workerCount = static_cast<f64>(cpuScheduler.workerThreadCount());
        const f64 logicalWorkerSlotCount = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(workerCount), VectorDoubleReplicate(1.0)));
        outRecordedGraph.addRecordingElapsedSeconds(elapsedSeconds, artifactOperation);
        const f64 availableWorkerSeconds = VectorDoubleGetX(VectorDoubleMultiply(VectorDoubleReplicate(elapsedSeconds), VectorDoubleReplicate(logicalWorkerSlotCount)));
        outRecordedGraph.addReadyFrontierStatistics(elapsedSeconds, workerBusySeconds, availableWorkerSeconds, artifactOperation);
    };

    if(!usesParallelRecording){
        if(!recordPreparedPacketRangeInCompileOrder(graph, declarationAccess, compiledGraph, planAccess, artifactOperation, range, serialStateFanInScratchArena, *serialScratch, outRecordedGraph, outCommandIrCapture, outFailedPacket))
            return false;
        completeReadyFrontierTelemetry();
        return true;
    }
    if(!outRecordedGraph.ensureWorkerRecordingScratch(static_cast<usize>(cpuScheduler.workerThreadCount()) + 1u, artifactOperation))
        return false;

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
                if(!planAccess.validPacket(sourcePacket) || sourcePacket.index >= packet.index || !outRecordedGraph.findWithinArtifactOperation(sourcePacket, artifactOperation))
                    return false;
            }
        }
        return true;
    };
    if(!recordingFrontiersAreMonotonic){
        Sort(recordingEntries.begin(), recordingEntries.end(), __hidden_gpu_packet_runtime_recording_frontier::LessPacketRecordingFrontierEntry);
    }

    usize frontierBegin = 0u;
    while(frontierBegin < recordingEntries.size()){
        usize frontierEnd = frontierBegin + 1u;
        while(frontierEnd < recordingEntries.size() && recordingEntries[frontierEnd].frontier == recordingEntries[frontierBegin].frontier)
            ++frontierEnd;

        serialPacketIndices.clear();
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
            else
                serialPacketIndices.push_back(packet.index);
        }

        if(parallelPacketIndices.empty()){
            for(const u32 packetIndex : serialPacketIndices){
                const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
                if(!recordPacket(graph, declarationAccess, compiledGraph, planAccess, artifactOperation, packet, 0u, 0u, serialStateFanInScratchArena, *serialScratch, outRecordedGraph)){
                    if(outFailedPacket)
                        *outFailedPacket = packet;
                    return false;
                }
            }
        }
        else if(serialPacketIndices.empty()){
            if(parallelPacketIndices.size() == 1u){
                const GpuSubmissionPacketId packet = planAccess.packetIdAt(parallelPacketIndices[0u]);
                const usize workerSlot = cpuScheduler.currentWorkerIndex();
                GpuRecordedGraph::PacketRecordingScratch* const scratch = outRecordedGraph.workerRecordingScratch(workerSlot, artifactOperation);
                if(
                    !scratch
                    || !scratch->stateFanInScratchArena
                    || !recordPacket(
                        graph,
                        declarationAccess,
                        compiledGraph,
                        planAccess,
                        artifactOperation,
                        packet,
                        cpuScheduler.domainIdentity(),
                        static_cast<u32>(workerSlot + 1u),
                        *scratch->stateFanInScratchArena,
                        *scratch,
                        outRecordedGraph
                    )
                ){
                    if(outFailedPacket)
                        *outFailedPacket = packet;
                    return false;
                }
            }
            else{
                parallelResults.resize(parallelPacketIndices.size());
                parallelAborts.resize(parallelPacketIndices.size());
                const u64 recordingAttemptGeneration = outRecordedGraph.recordingAttemptGenerationWithinArtifactOperation(artifactOperation);
                ReadyFrontierRecordingUnwindScope recordingUnwind(graph, compiledGraph, planAccess, artifactOperation, parallelPacketIndices, recordingAttemptGeneration, outRecordedGraph, parallelAborts);

                const auto parallelWork = cpuScheduler.parallelFor(0u, parallelPacketIndices.size(), [&](const usize parallelIndex){
                    const GpuSubmissionPacketId packet = planAccess.packetIdAt(parallelPacketIndices[parallelIndex]);
                    const usize workerSlot = cpuScheduler.currentWorkerIndex();
                    GpuRecordedGraph::PacketRecordingScratch* const scratch = outRecordedGraph.workerRecordingScratch(workerSlot, artifactOperation);
                    if(!scratch || !scratch->stateFanInScratchArena){
                        parallelResults[parallelIndex] = 0u;
                        return;
                    }
                    parallelResults[parallelIndex] = recordPacket(
                        graph,
                        declarationAccess,
                        compiledGraph,
                        planAccess,
                        artifactOperation,
                        packet,
                        cpuScheduler.domainIdentity(),
                        static_cast<u32>(workerSlot + 1u),
                        *scratch->stateFanInScratchArena,
                        *scratch,
                        outRecordedGraph,
                        nullptr,
                        &parallelAborts[parallelIndex]
                    ) ? 1u : 0u;
                });
                for(usize parallelIndex = 0u; parallelIndex < parallelAborts.size(); ++parallelIndex){
                    GpuPacketRecordingAbort& abort = parallelAborts[parallelIndex];
                    if(!abort.valid())
                        continue;
                    const GpuSubmissionPacketId packet = planAccess.packetIdAt(parallelPacketIndices[parallelIndex]);
                    outRecordedGraph.discardPacketTimingTicket(packet, artifactOperation);
                    const b8 abortCompleted = graph.completePacketRecordingAbort(compiledGraph, planAccess, abort);
                    FOUNDATION_FATAL_ASSERT_MSG(abortCompleted, "joined packet drain must consume every deferred recording abort");
                }
                const b8 recordingAttemptResolved = graph.resolveRecordingAttemptIfTerminal(compiledGraph, recordingAttemptGeneration);
                FOUNDATION_FATAL_ASSERT_MSG(recordingAttemptResolved, "joined packet drain must preserve or resolve its exact recording-plan lease");
                recordingUnwind.release();
                if(!parallelWork){
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("GpuNativePacketRecorder: packet recording rejected CPU work: {}"), static_cast<u32>(parallelWork.error()));
                    if(outFailedPacket)
                        *outFailedPacket = planAccess.packetIdAt(parallelPacketIndices.front());
                    return false;
                }
                outRecordedGraph.cachePacketRecordingOverlaps(compiledGraph, planAccess, parallelPacketIndices, artifactOperation, scratchArena);
                for(usize parallelIndex = 0u; parallelIndex < parallelResults.size(); ++parallelIndex){
                    if(parallelResults[parallelIndex] != 0u)
                        continue;
                    if(outFailedPacket)
                        *outFailedPacket = planAccess.packetIdAt(parallelPacketIndices[parallelIndex]);
                    return false;
                }
            }
        }
        else{
            const usize parallelCount = parallelPacketIndices.size();
            const auto recordParallelPacket = [&](const usize parallelIndex, GpuPacketRecordingAbort* const deferredAbort)noexcept(false){
                const GpuSubmissionPacketId packet = planAccess.packetIdAt(parallelPacketIndices[parallelIndex]);
                const usize workerSlot = cpuScheduler.currentWorkerIndex();
                GpuRecordedGraph::PacketRecordingScratch* const scratch = outRecordedGraph.workerRecordingScratch(workerSlot, artifactOperation);
                if(!scratch || !scratch->stateFanInScratchArena)
                    return false;
                return recordPacket(
                    graph,
                    declarationAccess,
                    compiledGraph,
                    planAccess,
                    artifactOperation,
                    packet,
                    cpuScheduler.domainIdentity(),
                    static_cast<u32>(workerSlot + 1u),
                    *scratch->stateFanInScratchArena,
                    *scratch,
                    outRecordedGraph,
                    nullptr,
                    deferredAbort
                );
            };
            if(cpuScheduler.workerThreadCount() == 0u){
                for(const u32 packetIndex : serialPacketIndices){
                    const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
                    if(!recordPacket(graph, declarationAccess, compiledGraph, planAccess, artifactOperation, packet, 0u, 0u, serialStateFanInScratchArena, *serialScratch, outRecordedGraph)){
                        if(outFailedPacket)
                            *outFailedPacket = packet;
                        return false;
                    }
                }
                for(usize parallelIndex = 0u; parallelIndex < parallelCount; ++parallelIndex){
                    if(!recordParallelPacket(parallelIndex, nullptr)){
                        if(outFailedPacket)
                            *outFailedPacket = planAccess.packetIdAt(parallelPacketIndices[parallelIndex]);
                        return false;
                    }
                }
            }
            else{
                parallelResults.assign(parallelCount, 0u);
                parallelAborts.resize(parallelCount);
                const u64 recordingAttemptGeneration = outRecordedGraph.recordingAttemptGenerationWithinArtifactOperation(artifactOperation);
                GpuPacketRecordingAbort serialAbort;
                u32 serialAbortPacketIndex = Limit<u32>::s_Max;
                ReadyFrontierRecordingUnwindScope recordingUnwind(graph, compiledGraph, planAccess, artifactOperation, parallelPacketIndices, recordingAttemptGeneration, outRecordedGraph, parallelAborts, &serialAbortPacketIndex, &serialAbort);

                alignas(64) Atomic<usize> nextParallelIndex{ 0u };
                Optional<CpuTaskAdmissionError::Enum> parallelSubmitError;
                GpuSubmissionPacketId serialFailedPacket;
                {
                    CpuTaskScope parallelScope(cpuScheduler);
                    const auto runParallelWork = [&]()noexcept(false){
                        for(;;){
                            const usize parallelIndex = nextParallelIndex.fetch_add(1u, MemoryOrder::relaxed);
                            if(parallelIndex >= parallelCount)
                                break;
                            parallelResults[parallelIndex] = recordParallelPacket(parallelIndex, &parallelAborts[parallelIndex]) ? 1u : 0u;
                        }
                    };
                    const usize workerTaskCount = Min<usize>(parallelCount, static_cast<usize>(cpuScheduler.workerThreadCount()));
                    for(usize taskIndex = 0u; taskIndex < workerTaskCount; ++taskIndex){
                        const auto submitted = parallelScope.submit([&runParallelWork](){ runParallelWork(); });
                        if(!submitted){
                            parallelSubmitError = submitted.error();
                            nextParallelIndex.store(parallelCount, MemoryOrder::release);
                            break;
                        }
                    }
                    if(!parallelSubmitError){
                        try{
                            for(const u32 packetIndex : serialPacketIndices){
                                const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
                                serialAbortPacketIndex = packetIndex;
                                if(!recordPacket(graph, declarationAccess, compiledGraph, planAccess, artifactOperation, packet, 0u, 0u, serialStateFanInScratchArena, *serialScratch, outRecordedGraph, nullptr, &serialAbort)){
                                    serialFailedPacket = packet;
                                    nextParallelIndex.store(parallelCount, MemoryOrder::release);
                                    break;
                                }
                            }
                            if(!serialFailedPacket.valid())
                                runParallelWork();
                        }
                        catch(...){
                            nextParallelIndex.store(parallelCount, MemoryOrder::release);
                            parallelScope.wait();
                            throw;
                        }
                    }
                    parallelScope.wait();
                }
                if(serialAbort.valid()){
                    const GpuSubmissionPacketId packet = planAccess.packetIdAt(serialAbortPacketIndex);
                    outRecordedGraph.discardPacketTimingTicket(packet, artifactOperation);
                    const b8 abortCompleted = graph.completePacketRecordingAbort(compiledGraph, planAccess, serialAbort);
                    FOUNDATION_FATAL_ASSERT_MSG(abortCompleted, "joined packet drain must consume every deferred serial recording abort");
                }
                for(usize parallelIndex = 0u; parallelIndex < parallelAborts.size(); ++parallelIndex){
                    GpuPacketRecordingAbort& abort = parallelAborts[parallelIndex];
                    if(!abort.valid())
                        continue;
                    const GpuSubmissionPacketId packet = planAccess.packetIdAt(parallelPacketIndices[parallelIndex]);
                    outRecordedGraph.discardPacketTimingTicket(packet, artifactOperation);
                    const b8 abortCompleted = graph.completePacketRecordingAbort(compiledGraph, planAccess, abort);
                    FOUNDATION_FATAL_ASSERT_MSG(abortCompleted, "joined packet drain must consume every deferred recording abort");
                }
                const b8 recordingAttemptResolved = graph.resolveRecordingAttemptIfTerminal(compiledGraph, recordingAttemptGeneration);
                FOUNDATION_FATAL_ASSERT_MSG(recordingAttemptResolved, "joined packet drain must preserve or resolve its exact recording-plan lease");
                recordingUnwind.release();
                if(parallelSubmitError){
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("GpuNativePacketRecorder: packet recording rejected CPU work: {}"), static_cast<u32>(*parallelSubmitError));
                    if(outFailedPacket)
                        *outFailedPacket = planAccess.packetIdAt(parallelPacketIndices.front());
                    return false;
                }
                if(serialFailedPacket.valid()){
                    if(outFailedPacket)
                        *outFailedPacket = serialFailedPacket;
                    return false;
                }
                outRecordedGraph.cachePacketRecordingOverlaps(compiledGraph, planAccess, parallelPacketIndices, artifactOperation, scratchArena);
                for(usize parallelIndex = 0u; parallelIndex < parallelResults.size(); ++parallelIndex){
                    if(parallelResults[parallelIndex] != 0u)
                        continue;
                    if(outFailedPacket)
                        *outFailedPacket = planAccess.packetIdAt(parallelPacketIndices[parallelIndex]);
                    return false;
                }
            }
        }
        parallelAborts.clear();
        frontierBegin = frontierEnd;
    }
    completeReadyFrontierTelemetry();
    return true;
}


b8 GpuNativePacketRecorder::recordPreparedPacketRangeInCompileOrder(
    const GpuTaskGraph& graph,
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuRecordedGraph::ArtifactOperation& artifactAccess,
    const GpuSubmissionPacketRange& range,
    Alloc::ScratchArena& stateFanInScratchArena,
    GpuRecordedGraph::PacketRecordingScratch& scratch,
    GpuRecordedGraph& outRecordedGraph,
    GpuCommandIrCapture* const outCommandIrCapture,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    const usize rangeBegin = range.first.index;
    const usize rangeEnd = rangeBegin + range.packetCount;
    for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
        if(!recordPacket(graph, declarationAccess, compiledGraph, planAccess, artifactAccess, packet, 0u, 0u, stateFanInScratchArena, scratch, outRecordedGraph, outCommandIrCapture)){
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

