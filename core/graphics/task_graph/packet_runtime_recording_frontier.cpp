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
    Vector<GpuSubmissionPacketId, Alloc::ScratchArena> recordingPackets(scratchArena);
    Vector<u32, Alloc::ScratchArena> effectiveRecordingFrontiers(scratchArena);
    recordingPackets.reserve(range.packetCount);
    effectiveRecordingFrontiers.reserve(range.packetCount);
    u32 maximumFrontier = 0u;
    for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
        recordingPackets.push_back(packet);

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
            if(sourceDescIndex >= effectiveRecordingFrontiers.size())
                return false;
            const u32 sourceFrontier = effectiveRecordingFrontiers[sourceDescIndex];
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
        effectiveRecordingFrontiers.push_back(effectiveFrontier);
        maximumFrontier = Max(maximumFrontier, effectiveFrontier);
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
        for(usize recordingIndex = 0u; recordingIndex < recordingPackets.size(); ++recordingIndex){
            const GpuSubmissionPacketId packet = recordingPackets[recordingIndex];
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

    Vector<usize, Alloc::ScratchArena> parallelRecordingIndices(scratchArena);
    Vector<u8, Alloc::ScratchArena> parallelResults(scratchArena);
    for(u32 frontier = 0u;; ++frontier){
        parallelRecordingIndices.clear();
        for(usize recordingIndex = 0u; recordingIndex < recordingPackets.size(); ++recordingIndex){
            const GpuSubmissionPacketId packet = recordingPackets[recordingIndex];
            if(effectiveRecordingFrontiers[recordingIndex] != frontier)
                continue;
            if(!packetStateSeedsAreRecorded(packet)){
                if(outFailedPacket)
                    *outFailedPacket = packet;
                return false;
            }
            if(packetAllowsParallelRecording(packet))
                parallelRecordingIndices.push_back(recordingIndex);
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

        if(!parallelRecordingIndices.empty()){
            parallelResults.resize(parallelRecordingIndices.size());
            workerPool.parallelFor(0u, parallelRecordingIndices.size(), [&](const usize parallelIndex){
                const GpuSubmissionPacketId packet = recordingPackets[parallelRecordingIndices[parallelIndex]];
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
            for(usize parallelIndex = 0u; parallelIndex < parallelResults.size(); ++parallelIndex){
                if(parallelResults[parallelIndex] != 0u)
                    continue;
                if(outFailedPacket)
                    *outFailedPacket = recordingPackets[parallelRecordingIndices[parallelIndex]];
                return false;
            }
        }
        if(frontier == maximumFrontier)
            break;
    }
    completeReadyFrontierTelemetry();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

