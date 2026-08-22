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


bool GpuNativePacketRecorder::recordPacketRangeInCompileOrder(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketRange& range,
    const GpuNativePacketRecordDesc* const recordOverrides,
    const usize recordOverrideCount,
    GpuRecordedGraph& outRecordedGraph,
    GpuSubmissionPacketId* const outFailedPacket,
    GpuCommandIrCapture* const commandIrCapture,
    const GpuTaskPacketStateBinding* const taskStateBindings,
    const usize taskStateBindingCount
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    if(
        !compiledGraph.validFor(graph)
        || !graph.validForDeviceGeneration(compiledGraph.deviceGeneration())
        || m_device.getDeviceGeneration() != compiledGraph.deviceGeneration()
        || !compiledGraph.validPacketRange(range)
        || (recordOverrideCount != 0u && !recordOverrides)
        || !__hidden_gpu_packet_runtime_recording_frontier::ValidateTaskPacketStateBindings(
            graph,
            compiledGraph,
            taskStateBindings,
            taskStateBindingCount
        )
    )
        return false;

    const usize rangeBegin = range.first.index;
    const usize rangeEnd = rangeBegin + range.packetCount;
    for(usize overrideIndex = 0u; overrideIndex < recordOverrideCount; ++overrideIndex){
        const GpuNativePacketRecordDesc& overrideDesc = recordOverrides[overrideIndex];
        if(
            !compiledGraph.validPacket(overrideDesc.packet)
            || overrideDesc.packet.index < rangeBegin
            || overrideDesc.packet.index >= rangeEnd
        )
            return false;
        for(usize previousOverrideIndex = 0u; previousOverrideIndex < overrideIndex; ++previousOverrideIndex){
            if(recordOverrides[previousOverrideIndex].packet == overrideDesc.packet)
                return false;
        }
    }
    if(!prepareRecordingAttempt(graph, compiledGraph, range, outRecordedGraph))
        return false;

    // The compiler emits packet IDs in stable topological order, so native recording preserves the graph's
    // internal state-seed chain without requiring renderer-side packet collectors. Callers supply only sparse
    // external-state overrides and can retain an intentional late tail outside this range.
    for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
        GpuNativePacketRecordDesc defaultDesc{
            .packet = packet,
        };
        const GpuNativePacketRecordDesc* recordDesc = &defaultDesc;
        for(usize overrideIndex = 0u; overrideIndex < recordOverrideCount; ++overrideIndex){
            if(recordOverrides[overrideIndex].packet == packet){
                recordDesc = &recordOverrides[overrideIndex];
                break;
            }
        }
        if(!recordPacket(
            graph,
            compiledGraph,
            *recordDesc,
            outRecordedGraph,
            commandIrCapture,
            taskStateBindings,
            taskStateBindingCount
        )){
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }
    }
    return true;
}


bool GpuNativePacketRecorder::recordTaskRangeInCompileOrder(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskId firstTask,
    const GpuTaskId lastTask,
    const GpuNativePacketRecordDesc* const recordOverrides,
    const usize recordOverrideCount,
    GpuRecordedGraph& outRecordedGraph,
    GpuSubmissionPacketId* const outFailedPacket,
    GpuCommandIrCapture* const commandIrCapture,
    const GpuTaskPacketStateBinding* const taskStateBindings,
    const usize taskStateBindingCount
)const{
    return recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        compiledGraph.packetRangeForTasks(firstTask, lastTask),
        recordOverrides,
        recordOverrideCount,
        outRecordedGraph,
        outFailedPacket,
        commandIrCapture,
        taskStateBindings,
        taskStateBindingCount
    );
}


bool GpuNativePacketRecorder::recordPacketRangeInReadyFrontiers(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketRange& range,
    const GpuNativePacketRecordDesc* const recordOverrides,
    const usize recordOverrideCount,
    GpuRecordedGraph& outRecordedGraph,
    Alloc::ThreadPool& workerPool,
    GpuSubmissionPacketId* const outFailedPacket,
    GpuCommandIrCapture* const commandIrCapture,
    const GpuTaskPacketStateBinding* const taskStateBindings,
    const usize taskStateBindingCount
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    if(
        !compiledGraph.validFor(graph)
        || !graph.validForDeviceGeneration(compiledGraph.deviceGeneration())
        || m_device.getDeviceGeneration() != compiledGraph.deviceGeneration()
        || !compiledGraph.validPacketRange(range)
        || (recordOverrideCount != 0u && !recordOverrides)
        || !__hidden_gpu_packet_runtime_recording_frontier::ValidateTaskPacketStateBindings(
            graph,
            compiledGraph,
            taskStateBindings,
            taskStateBindingCount
        )
    )
        return false;

    // Command-IR records form one linear graph-generation artifact. Keeping capture serial preserves its existing
    // record order and rollback contract; native packets remain eligible for worker recording when capture is off.
    if(
        commandIrCapture
        || !workerPool.isParallelEnabled()
        || range.packetCount < 2u
    ){
        return recordPacketRangeInCompileOrder(
            graph,
            compiledGraph,
            range,
            recordOverrides,
            recordOverrideCount,
            outRecordedGraph,
            outFailedPacket,
            commandIrCapture,
            taskStateBindings,
            taskStateBindingCount
        );
    }

    const usize rangeBegin = range.first.index;
    const usize rangeEnd = rangeBegin + range.packetCount;
    for(usize overrideIndex = 0u; overrideIndex < recordOverrideCount; ++overrideIndex){
        const GpuNativePacketRecordDesc& overrideDesc = recordOverrides[overrideIndex];
        if(
            !compiledGraph.validPacket(overrideDesc.packet)
            || overrideDesc.packet.index < rangeBegin
            || overrideDesc.packet.index >= rangeEnd
        )
            return false;
        for(usize previousOverrideIndex = 0u; previousOverrideIndex < overrideIndex; ++previousOverrideIndex){
            if(recordOverrides[previousOverrideIndex].packet == overrideDesc.packet)
                return false;
        }
    }
    if(!prepareRecordingAttempt(graph, compiledGraph, range, outRecordedGraph))
        return false;

    Alloc::ScratchArena scratchArena(__hidden_gpu_packet_runtime_recording_frontier::s_PacketRecordingFrontierScratchArena);
    Vector<GpuNativePacketRecordDesc, Alloc::ScratchArena> recordDescs(scratchArena);
    recordDescs.reserve(range.packetCount);
    u32 maximumFrontier = 0u;
    for(usize packetIndex = rangeBegin; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
        GpuNativePacketRecordDesc desc{
            .packet = packet,
        };
        for(usize overrideIndex = 0u; overrideIndex < recordOverrideCount; ++overrideIndex){
            if(recordOverrides[overrideIndex].packet == packet){
                desc = recordOverrides[overrideIndex];
                break;
            }
        }
        recordDescs.push_back(desc);
        const u32 frontier = compiledGraph.packet(packet).recordingFrontier;
        if(frontier > maximumFrontier)
            maximumFrontier = frontier;
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
    const auto packetAllowsParallelRecording = [&](const GpuNativePacketRecordDesc& desc){
        // Raw record-descriptor sources and late task bindings still bridge work outside the compiler's internal
        // packet edges. Preserve serial recording for those runtime inputs. Task-declared sources are immutable
        // graph-owned snapshots, so an explicitly opted-in task may safely consume them on a worker frontier.
        if(desc.externalStateSourceCount != 0u)
            return false;

        const GpuSubmissionPacketId packet = desc.packet;
        for(usize bindingIndex = 0u; bindingIndex < taskStateBindingCount; ++bindingIndex){
            if(compiledGraph.packetForTask(taskStateBindings[bindingIndex].task) == packet)
                return false;
        }
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

    Vector<usize, Alloc::ScratchArena> parallelDescIndices(scratchArena);
    Vector<u8, Alloc::ScratchArena> parallelResults(scratchArena);
    for(u32 frontier = 0u;; ++frontier){
        parallelDescIndices.clear();
        for(usize descIndex = 0u; descIndex < recordDescs.size(); ++descIndex){
            const GpuNativePacketRecordDesc& desc = recordDescs[descIndex];
            if(compiledGraph.packet(desc.packet).recordingFrontier != frontier)
                continue;
            if(!packetStateSeedsAreRecorded(desc.packet)){
                if(outFailedPacket)
                    *outFailedPacket = desc.packet;
                return false;
            }
            if(packetAllowsParallelRecording(desc))
                parallelDescIndices.push_back(descIndex);
            else if(!recordPacketWithScratch(
                graph,
                compiledGraph,
                desc,
                outRecordedGraph,
                outRecordedGraph.m_serialRecordingScratch,
                nullptr,
                taskStateBindings,
                taskStateBindingCount
            )){
                if(outFailedPacket)
                    *outFailedPacket = desc.packet;
                return false;
            }
        }

        if(!parallelDescIndices.empty()){
            parallelResults.resize(parallelDescIndices.size());
            workerPool.parallelFor(0u, parallelDescIndices.size(), [&](const usize parallelIndex){
                const GpuNativePacketRecordDesc& desc = recordDescs[parallelDescIndices[parallelIndex]];
                GpuRecordedGraph::PacketRecordingScratch* const scratch = outRecordedGraph.packetRecordingScratch(desc.packet);
                GpuNativePacketRecordDesc workerDesc = desc;
                // Reserve zero for serial/direct command lists. ThreadPool's caller is worker zero, so shift every
                // ready-frontier lease by one and retain a stable distinction even when the caller records a chunk.
                workerDesc.recordingWorkerIndex = static_cast<u32>(workerPool.currentWorkerIndex() + 1u);
                parallelResults[parallelIndex] = scratch && recordPacketWithScratch(
                    graph,
                    compiledGraph,
                    workerDesc,
                    outRecordedGraph,
                    *scratch,
                    nullptr,
                    taskStateBindings,
                    taskStateBindingCount
                ) ? 1u : 0u;
            });
            for(usize parallelIndex = 0u; parallelIndex < parallelResults.size(); ++parallelIndex){
                if(parallelResults[parallelIndex] != 0u)
                    continue;
                if(outFailedPacket)
                    *outFailedPacket = recordDescs[parallelDescIndices[parallelIndex]].packet;
                return false;
            }
        }
        if(frontier == maximumFrontier)
            break;
    }
    return true;
}


bool GpuNativePacketRecorder::recordTaskRangeInReadyFrontiers(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskId firstTask,
    const GpuTaskId lastTask,
    const GpuNativePacketRecordDesc* const recordOverrides,
    const usize recordOverrideCount,
    GpuRecordedGraph& outRecordedGraph,
    Alloc::ThreadPool& workerPool,
    GpuSubmissionPacketId* const outFailedPacket,
    GpuCommandIrCapture* const commandIrCapture,
    const GpuTaskPacketStateBinding* const taskStateBindings,
    const usize taskStateBindingCount
)const{
    return recordPacketRangeInReadyFrontiers(
        graph,
        compiledGraph,
        compiledGraph.packetRangeForTasks(firstTask, lastTask),
        recordOverrides,
        recordOverrideCount,
        outRecordedGraph,
        workerPool,
        outFailedPacket,
        commandIrCapture,
        taskStateBindings,
        taskStateBindingCount
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

