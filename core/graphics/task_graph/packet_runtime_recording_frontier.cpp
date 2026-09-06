// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"

#include <core/graphics/backend_selection.h>
#include <global/termination.h>


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
        Vector<GpuTaskGraph::PacketRecordingAbort, Alloc::ScratchArena>& aborts,
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
            GpuTaskGraph::PacketRecordingAbort& abort = m_aborts[parallelIndex];
            if(!abort.valid())
                continue;
            const GpuSubmissionPacketId packet = m_planAccess.packetIdAt(m_packetIndices[parallelIndex]);
            m_recordedGraph.abandonPacketTimingTicketWithoutCallbacks(packet, m_artifactOperation);
            if(!m_graph.abandonPacketRecordingAbortWithoutCallbacks(m_compiledGraph, m_planAccess, abort)){
                NWB_FATAL_ASSERT_MSG(false, "joined packet unwind must consume every deferred recording abort");
                TerminateInvariant();
            }
        }
        if(!m_graph.resolveRecordingAttemptIfTerminal(m_compiledGraph, m_recordingAttemptGeneration)){
            NWB_FATAL_ASSERT_MSG(false, "joined packet unwind must preserve or resolve its exact recording-plan lease");
            TerminateInvariant();
        }
    }

    void release()noexcept{ m_active = false; }

private:
    const GpuTaskGraph& m_graph;
    const GpuCompiledGraph& m_compiledGraph;
    const GpuCompiledGraph::ReadView& m_planAccess;
    GpuRecordedGraph& m_recordedGraph;
    const GpuRecordedGraph::ArtifactOperation& m_artifactOperation;
    const Vector<u32, Alloc::ScratchArena>& m_packetIndices;
    Vector<GpuTaskGraph::PacketRecordingAbort, Alloc::ScratchArena>& m_aborts;
    u64 m_recordingAttemptGeneration = 0u;
    bool m_active = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_recording_frontier{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_PacketRecordingFrontierScratchArena("graphics/task_graph/packet_recording_frontier");

struct PacketRecordingFrontierEntry{
    GpuSubmissionPacketId packet;
    u32 frontier = 0u;
    bool allowsParallelRecording = false;
};

[[nodiscard]] bool LessPacketRecordingFrontierEntry(
    const PacketRecordingFrontierEntry& left,
    const PacketRecordingFrontierEntry& right
)noexcept{
    return left.frontier < right.frontier
        || (left.frontier == right.frontier && left.packet.index < right.packet.index)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuNativePacketRecorder::recordPacketRangeInCompileOrder(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketRange& range,
    GpuRecordedGraph& outRecordedGraph,
    GpuSubmissionPacketId* const outFailedPacket,
    GpuCommandIrCapture* const commandIrCapture
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    GpuRecordedGraph::ArtifactOperation artifactOperation(
        outRecordedGraph,
        GpuRecordedGraph::ArtifactOperationMode::Exclusive
    );
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
    {
        GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::tryAcquire(graph);
        if(
            !declarationAccess.valid()
            || !prepareRecordingAttempt(
                graph,
                compiledGraph,
                range,
                outRecordedGraph,
                declarationAccess,
                planAccess,
                artifactOperation
            )
        )
            return false;
    }
    GpuRecordedGraph::PacketRecordingScratch* const serialScratch =
        outRecordedGraph.serialRecordingScratch(artifactOperation)
    ;
    if(!serialScratch || !serialScratch->stateFanInScratchArena)
        return false;
    Alloc::ScratchArena& stateFanInScratchArena = *serialScratch->stateFanInScratchArena;
    // The compiler emits packet IDs in stable topological order, so native recording preserves the graph's
    // internal state-seed chain without requiring renderer-side packet collectors.
    for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
        if(!recordPacket(
            graph,
            compiledGraph,
            planAccess,
            artifactOperation,
            packet,
            outRecordedGraph,
            *serialScratch,
            stateFanInScratchArena,
            commandIrCapture
        )){
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }
    }
    outRecordedGraph.addRecordingElapsedSeconds(
        DurationInSeconds<f64>(TimerNow(), recordingOperationBegin),
        artifactOperation
    );
    return true;
}


bool GpuNativePacketRecorder::recordTaskRangeInCompileOrder(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskId firstTask,
    const GpuTaskId lastTask,
    GpuRecordedGraph& outRecordedGraph,
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
    return recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        range,
        outRecordedGraph,
        outFailedPacket,
        commandIrCapture
    );
}


bool GpuNativePacketRecorder::recordPacketRangeInReadyFrontiers(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketRange& range,
    GpuRecordedGraph& outRecordedGraph,
    Alloc::ThreadPool& workerPool,
    GpuSubmissionPacketId* const outFailedPacket,
    GpuCommandIrCapture* const commandIrCapture
)const{
    using RecordingEntry = __hidden_gpu_packet_runtime_recording_frontier::PacketRecordingFrontierEntry;

    if(outFailedPacket)
        *outFailedPacket = {};
    GpuRecordedGraph::ArtifactOperation artifactOperation(
        outRecordedGraph,
        GpuRecordedGraph::ArtifactOperationMode::Exclusive
    );
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
    Alloc::ScratchArena scratchArena(__hidden_gpu_packet_runtime_recording_frontier::s_PacketRecordingFrontierScratchArena);
    Vector<RecordingEntry, Alloc::ScratchArena> recordingEntries(scratchArena);
    recordingEntries.reserve(range.packetCount);
    Vector<u32, Alloc::ScratchArena> parallelPacketIndices(scratchArena);
    Vector<u8, Alloc::ScratchArena> parallelResults(scratchArena);
    Vector<GpuTaskGraph::PacketRecordingAbort, Alloc::ScratchArena> parallelAborts(scratchArena);
    parallelPacketIndices.reserve(range.packetCount);
    parallelResults.reserve(range.packetCount);
    parallelAborts.reserve(range.packetCount);
    bool recordingFrontiersAreMonotonic = true;
    {
        GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::tryAcquire(graph);
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
            const auto raiseFromSourcePacket = [&](const GpuSubmissionPacketId sourcePacket){
                if(
                    !planAccess.validPacket(sourcePacket)
                    || sourcePacket == packet
                    || sourcePacket.index >= packet.index
                )
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
                if(
                    task.id != tasks[taskIndex]
                    || !compiledTaskView.valid()
                )
                    return false;
                allowsParallelRecording = allowsParallelRecording && task.scheduling.allowParallelRecording;
                for(u32 seedIndex = 0u; seedIndex < compiledTask->prologueStateSeedCount; ++seedIndex){
                    if(!raiseFromSourcePacket(stateSeeds[seedIndex].sourcePacket))
                        return false;
                }
            }
            if(!recordingEntries.empty() && effectiveFrontier < recordingEntries.back().frontier)
                recordingFrontiersAreMonotonic = false;
            recordingEntries.push_back(RecordingEntry{
                .packet = packet,
                .frontier = effectiveFrontier,
                .allowsParallelRecording = allowsParallelRecording,
            });
        }
        if(!prepareRecordingAttempt(
            graph,
            compiledGraph,
            range,
            outRecordedGraph,
            declarationAccess,
            planAccess,
            artifactOperation
        ))
            return false;
    }
    GpuRecordedGraph::PacketRecordingScratch* const serialScratch =
        outRecordedGraph.serialRecordingScratch(artifactOperation)
    ;
    if(!serialScratch || !serialScratch->stateFanInScratchArena)
        return false;
    Alloc::ScratchArena& serialStateFanInScratchArena = *serialScratch->stateFanInScratchArena;
    const auto completeReadyFrontierTelemetry = [&]{
        const f64 elapsedSeconds = DurationInSeconds<f64>(TimerNow(), recordingOperationBegin);
        f64 workerBusySeconds = 0.0;
        for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
            const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
            const GpuRecordedPacket* const recordedPacket = outRecordedGraph.findWithinArtifactOperation(
                packet,
                artifactOperation
            );
            NWB_ASSERT(recordedPacket);
            if(recordedPacket)
                workerBusySeconds += recordedPacket->recordingSeconds;
        }

        // ThreadPool workers and its calling thread are all callable logical recording slots. Keeping the entire
        // successful ready-frontier operation in the denominator exposes serial fallbacks and underfilled frontiers.
        const f64 logicalWorkerSlotCount = static_cast<f64>(workerPool.workerThreadCount()) + 1.0;
        outRecordedGraph.addRecordingElapsedSeconds(elapsedSeconds, artifactOperation);
        outRecordedGraph.addReadyFrontierStatistics(
            elapsedSeconds,
            workerBusySeconds,
            elapsedSeconds * logicalWorkerSlotCount,
            artifactOperation
        );
    };

    // Command-IR records form one linear graph-generation artifact. Keeping capture serial preserves its existing
    // record order and rollback contract. This path records directly after the shared prepare step so the enclosing
    // ready-frontier operation owns exactly one elapsed span rather than nesting compile-order telemetry.
    if(
        commandIrCapture
        || !workerPool.isParallelEnabled()
        || range.packetCount < 2u
    ){
        for(const RecordingEntry& entry : recordingEntries){
            const GpuSubmissionPacketId packet = entry.packet;
            if(recordPacket(
                graph,
                compiledGraph,
                planAccess,
                artifactOperation,
                packet,
                outRecordedGraph,
                *serialScratch,
                serialStateFanInScratchArena,
                commandIrCapture
            ))
                continue;
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }
        completeReadyFrontierTelemetry();
        return true;
    }

    const auto packetStateSeedsAreRecorded = [&](const GpuSubmissionPacketId packet){
        const GpuCompiledPacketView packetView = planAccess.packet(packet);
        if(!packetView.valid() || packetView.plan->taskCount == 0u)
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
                if(
                    !planAccess.validPacket(sourcePacket)
                    || sourcePacket == packet
                    || sourcePacket.index >= packet.index
                    || !outRecordedGraph.findWithinArtifactOperation(sourcePacket, artifactOperation)
                )
                    return false;
            }
        }
        return true;
    };
    // Compiler order is already free for monotonic frontiers, including a deep state-seed chain. A later independent
    // packet may lower the frontier again; sort that sparse case once instead of rescanning every packet per depth.
    if(!recordingFrontiersAreMonotonic){
        Sort(
            recordingEntries.begin(),
            recordingEntries.end(),
            __hidden_gpu_packet_runtime_recording_frontier::LessPacketRecordingFrontierEntry
        );
    }

    usize frontierBegin = 0u;
    while(frontierBegin < recordingEntries.size()){
        usize frontierEnd = frontierBegin + 1u;
        while(
            frontierEnd < recordingEntries.size()
            && recordingEntries[frontierEnd].frontier == recordingEntries[frontierBegin].frontier
        )
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
            else if(!recordPacket(
                graph,
                compiledGraph,
                planAccess,
                artifactOperation,
                packet,
                outRecordedGraph,
                *serialScratch,
                serialStateFanInScratchArena,
                nullptr
            )){
                if(outFailedPacket)
                    *outFailedPacket = packet;
                return false;
            }
        }

        if(!parallelPacketIndices.empty()){
            parallelResults.resize(parallelPacketIndices.size());
            parallelAborts.resize(parallelPacketIndices.size());
            const u64 recordingAttemptGeneration = outRecordedGraph.recordingAttemptGenerationWithinArtifactOperation(
                artifactOperation
            );
            ReadyFrontierRecordingUnwindScope recordingUnwind(
                graph,
                compiledGraph,
                planAccess,
                outRecordedGraph,
                artifactOperation,
                parallelPacketIndices,
                parallelAborts,
                recordingAttemptGeneration
            );

            workerPool.parallelFor(0u, parallelPacketIndices.size(), [&](const usize parallelIndex){
                const GpuSubmissionPacketId packet = planAccess.packetIdAt(parallelPacketIndices[parallelIndex]);
                GpuRecordedGraph::PacketRecordingScratch* const scratch = outRecordedGraph.packetRecordingScratch(
                    packet,
                    artifactOperation
                );
                if(!scratch || !scratch->stateFanInScratchArena){
                    parallelResults[parallelIndex] = 0u;
                    return;
                }
                // Reserve zero for serial/direct command lists. ThreadPool's caller is worker zero, so shift every
                // ready-frontier lease by one. The stable pool domain prevents a second ThreadPool with the same
                // local worker index from aliasing this native command-pool shard.
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
                    workerPool.domainIdentity(),
                    static_cast<u32>(workerPool.currentWorkerIndex() + 1u),
                    &parallelAborts[parallelIndex]
                ) ? 1u : 0u;
            });
            for(usize parallelIndex = 0u; parallelIndex < parallelAborts.size(); ++parallelIndex){
                GpuTaskGraph::PacketRecordingAbort& abort = parallelAborts[parallelIndex];
                if(!abort.valid())
                    continue;
                const GpuSubmissionPacketId packet = planAccess.packetIdAt(parallelPacketIndices[parallelIndex]);
                outRecordedGraph.discardPacketTimingTicket(packet, artifactOperation);
                if(!graph.completePacketRecordingAbort(compiledGraph, planAccess, abort)){
                    NWB_FATAL_ASSERT_MSG(false, "joined packet drain must consume every deferred recording abort");
                    TerminateInvariant();
                }
            }
            if(!graph.resolveRecordingAttemptIfTerminal(compiledGraph, recordingAttemptGeneration)){
                NWB_FATAL_ASSERT_MSG(false, "joined packet drain must preserve or resolve its exact recording-plan lease");
                TerminateInvariant();
            }
            recordingUnwind.release();
            outRecordedGraph.cachePacketRecordingOverlaps(
                compiledGraph,
                planAccess,
                parallelPacketIndices,
                scratchArena,
                artifactOperation
            );
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

