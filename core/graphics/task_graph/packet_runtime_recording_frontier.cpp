// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"

#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_recording_frontier{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_PacketRecordingFrontierScratchArena("graphics/task_graph/packet_recording_frontier");

struct PacketRecordingFrontierEntry{
    GpuSubmissionPacketId packet;
    u32 frontier = 0u;
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
    if(
        !compiledGraph.validFor(graph)
        || !graph.validForDeviceGeneration(compiledGraph.deviceGeneration())
        || m_device.getDeviceGeneration() != compiledGraph.deviceGeneration()
        || !compiledGraph.validPacketRange(range)
    )
        return false;

    const usize rangeBegin = range.first.index;
    const usize rangeEnd = rangeBegin + range.packetCount;
    const Timer recordingOperationBegin = TimerNow();
    if(!prepareRecordingAttempt(graph, compiledGraph, range, outRecordedGraph))
        return false;

    // The compiler emits packet IDs in stable topological order, so native recording preserves the graph's
    // internal state-seed chain without requiring renderer-side packet collectors.
    for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
        if(!recordPacket(
            graph,
            compiledGraph,
            packet,
            outRecordedGraph,
            outRecordedGraph.m_serialRecordingScratch,
            commandIrCapture
        )){
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }
    }
    outRecordedGraph.m_recordingElapsedSeconds += DurationInSeconds<f64>(TimerNow(), recordingOperationBegin);
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
    return recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        compiledGraph.packetRangeForTasks(firstTask, lastTask),
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
    if(
        !compiledGraph.validFor(graph)
        || !graph.validForDeviceGeneration(compiledGraph.deviceGeneration())
        || m_device.getDeviceGeneration() != compiledGraph.deviceGeneration()
        || !compiledGraph.validPacketRange(range)
    )
        return false;

    const usize rangeBegin = range.first.index;
    const usize rangeEnd = rangeBegin + range.packetCount;
    const Timer recordingOperationBegin = TimerNow();
    if(!prepareRecordingAttempt(graph, compiledGraph, range, outRecordedGraph))
        return false;

    Alloc::ScratchArena scratchArena(__hidden_gpu_packet_runtime_recording_frontier::s_PacketRecordingFrontierScratchArena);
    Vector<RecordingEntry, Alloc::ScratchArena> recordingEntries(scratchArena);
    recordingEntries.reserve(range.packetCount);
    bool recordingFrontiersAreMonotonic = true;
    for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);

        u32 effectiveFrontier = compiledGraph.packet(packet).recordingFrontier;
        if(effectiveFrontier == Limit<u32>::s_Max)
            return false;
        const auto raiseFromSourcePacket = [&](const GpuSubmissionPacketId sourcePacket){
            if(
                !compiledGraph.validPacket(sourcePacket)
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
            if(sourceFrontier == Limit<u32>::s_Max)
                return false;
            effectiveFrontier = Max(effectiveFrontier, sourceFrontier + 1u);
            return true;
        };

        const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
        const GpuTaskId* const tasks = compiledGraph.packetTasks(packet);
        if(!tasks || packetPlan.taskCount == 0u)
            return false;
        for(u32 taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuCompiledTask* const compiledTask = compiledGraph.findTask(tasks[taskIndex]);
            const GpuPacketStateSeed* const stateSeeds = compiledGraph.taskPrologueStateSeeds(tasks[taskIndex]);
            if(!compiledTask || (compiledTask->prologueStateSeedCount != 0u && !stateSeeds))
                return false;
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
        });
    }
    const auto completeReadyFrontierTelemetry = [&]{
        const f64 elapsedSeconds = DurationInSeconds<f64>(TimerNow(), recordingOperationBegin);
        f64 workerBusySeconds = 0.0;
        for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
            const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
            const GpuRecordedPacket& recordedPacket = outRecordedGraph.m_packets[packet.index];
            NWB_ASSERT(recordedPacket.commandListCount != 0u && recordedPacket.packet == packet);
            if(recordedPacket.commandListCount != 0u && recordedPacket.packet == packet)
                workerBusySeconds += recordedPacket.recordingSeconds;
        }

        // ThreadPool workers and its calling thread are all callable logical recording slots. Keeping the entire
        // successful ready-frontier operation in the denominator exposes serial fallbacks and underfilled frontiers.
        const f64 logicalWorkerSlotCount = static_cast<f64>(workerPool.workerThreadCount()) + 1.0;
        outRecordedGraph.m_recordingElapsedSeconds += elapsedSeconds;
        outRecordedGraph.m_readyFrontierElapsedSeconds += elapsedSeconds;
        outRecordedGraph.m_readyFrontierWorkerBusySeconds += workerBusySeconds;
        outRecordedGraph.m_readyFrontierWorkerCapacitySeconds += elapsedSeconds * logicalWorkerSlotCount;
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
                packet,
                outRecordedGraph,
                outRecordedGraph.m_serialRecordingScratch,
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
        if(!compiledGraph.validPacket(packet))
            return false;
        const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
        const GpuTaskId* const tasks = compiledGraph.packetTasks(packet);
        if(!tasks || packetPlan.taskCount == 0u)
            return false;

        for(u32 taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskId task = tasks[taskIndex];
            const GpuCompiledTask* const compiledTask = compiledGraph.findTask(task);
            if(!compiledTask)
                return false;

            const GpuPacketStateSeed* const stateSeeds = compiledGraph.taskPrologueStateSeeds(task);
            if(compiledTask->prologueStateSeedCount != 0u && !stateSeeds)
                return false;
            for(u32 stateSeedIndex = 0u; stateSeedIndex < compiledTask->prologueStateSeedCount; ++stateSeedIndex){
                const GpuSubmissionPacketId sourcePacket = stateSeeds[stateSeedIndex].sourcePacket;
                if(
                    !compiledGraph.validPacket(sourcePacket)
                    || sourcePacket == packet
                    || sourcePacket.index >= packet.index
                    || !outRecordedGraph.find(sourcePacket)
                )
                    return false;
            }
        }
        return true;
    };
    const auto packetAllowsParallelRecording = [&](const GpuSubmissionPacketId packet){
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

    // Compiler order is already free for monotonic frontiers, including a deep state-seed chain. A later independent
    // packet may lower the frontier again; sort that sparse case once instead of rescanning every packet per depth.
    if(!recordingFrontiersAreMonotonic){
        Sort(
            recordingEntries.begin(),
            recordingEntries.end(),
            __hidden_gpu_packet_runtime_recording_frontier::LessPacketRecordingFrontierEntry
        );
    }

    Vector<u32, Alloc::ScratchArena> parallelPacketIndices(scratchArena);
    Vector<u8, Alloc::ScratchArena> parallelResults(scratchArena);
    parallelPacketIndices.reserve(recordingEntries.size());
    parallelResults.reserve(recordingEntries.size());
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
            if(packetAllowsParallelRecording(packet))
                parallelPacketIndices.push_back(packet.index);
            else if(!recordPacket(
                graph,
                compiledGraph,
                packet,
                outRecordedGraph,
                outRecordedGraph.m_serialRecordingScratch,
                nullptr
            )){
                if(outFailedPacket)
                    *outFailedPacket = packet;
                return false;
            }
        }

        if(!parallelPacketIndices.empty()){
            parallelResults.resize(parallelPacketIndices.size());
            workerPool.parallelFor(0u, parallelPacketIndices.size(), [&](const usize parallelIndex){
                const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(parallelPacketIndices[parallelIndex]);
                GpuRecordedGraph::PacketRecordingScratch* const scratch = outRecordedGraph.packetRecordingScratch(packet);
                // Reserve zero for serial/direct command lists. ThreadPool's caller is worker zero, so shift every
                // ready-frontier lease by one. The stable pool domain prevents a second ThreadPool with the same
                // local worker index from aliasing this native command-pool shard.
                parallelResults[parallelIndex] = scratch && recordPacket(
                    graph,
                    compiledGraph,
                    packet,
                    outRecordedGraph,
                    *scratch,
                    nullptr,
                    workerPool.domainIdentity(),
                    static_cast<u32>(workerPool.currentWorkerIndex() + 1u)
                ) ? 1u : 0u;
            });
            outRecordedGraph.cachePacketRecordingOverlaps(compiledGraph, parallelPacketIndices, scratchArena);
            for(usize parallelIndex = 0u; parallelIndex < parallelResults.size(); ++parallelIndex){
                if(parallelResults[parallelIndex] != 0u)
                    continue;
                if(outFailedPacket)
                    *outFailedPacket = compiledGraph.packetIdAt(parallelPacketIndices[parallelIndex]);
                return false;
            }
        }
        frontierBegin = frontierEnd;
    }
    completeReadyFrontierTelemetry();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

