// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_execution{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        const bool hasRecordedProducer = binding.recordedProducerTask.valid();
        if(
            !graph.validTask(binding.task)
            || !compiledGraph.findTask(binding.task)
            || (!hasRecordedProducer && binding.externalStateSourceCount == 0u)
            || (binding.externalStateSourceCount != 0u && !binding.externalStateSources)
        )
            return false;
        if(hasRecordedProducer){
            if(
                !graph.validTask(binding.recordedProducerTask)
                || !compiledGraph.findTask(binding.recordedProducerTask)
            )
                return false;
            const GpuSubmissionPacketId consumerPacket = compiledGraph.packetForTask(binding.task);
            const GpuSubmissionPacketId producerPacket = compiledGraph.packetForTask(binding.recordedProducerTask);
            if(!compiledGraph.validPacket(consumerPacket) || !compiledGraph.validPacket(producerPacket))
                return false;
            if(producerPacket == consumerPacket){
                if(!compiledGraph.taskPrecedesInSamePacket(binding.recordedProducerTask, binding.task))
                    return false;
            }
            else if(producerPacket.index >= consumerPacket.index)
                return false;
        }
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
// A frontier packet is meaningful only as an explicit recovery/finalization tail. The compiler prevents it from
// merging with ordinary work, but does not force declaration order, so normal graph execution rejects one inside
// either its semantic endpoint prefix or its automatically derived ordinary prefix before recording begins.
[[nodiscard]] bool FindNormalGraphPacketRange(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskId& terminalTask,
    GpuSubmissionPacketRange& outRange,
    GpuSubmissionPacketId* const outFailedPacket
){
    outRange = {};
    if(outFailedPacket)
        *outFailedPacket = {};

    const usize packetCount = compiledGraph.packetCount();
    if(packetCount == 0u)
        return false;

    if(terminalTask.valid()){
        if(!graph.validTask(terminalTask) || !compiledGraph.findTask(terminalTask))
            return false;
        const GpuSubmissionPacketId terminalPacket = compiledGraph.packetForTask(terminalTask);
        if(!terminalPacket.valid())
            return false;
        for(usize packetIndex = 0u; packetIndex <= terminalPacket.index; ++packetIndex){
            const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
            if(!compiledGraph.packet(packet).joinsAcceptedQueueFrontier)
                continue;
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }

        outRange = compiledGraph.packetRange(compiledGraph.packetIdAt(0u), terminalPacket);
        return compiledGraph.validPacketRange(outRange);
    }

    usize firstFrontierPacketIndex = packetCount;
    for(usize packetIndex = 0u; packetIndex < packetCount; ++packetIndex){
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
        if(compiledGraph.packet(packet).joinsAcceptedQueueFrontier){
            if(firstFrontierPacketIndex == packetCount)
                firstFrontierPacketIndex = packetIndex;
            continue;
        }
        if(firstFrontierPacketIndex != packetCount){
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }
    }

    if(firstFrontierPacketIndex == 0u){
        if(outFailedPacket)
            *outFailedPacket = compiledGraph.packetIdAt(0u);
        return false;
    }

    const GpuSubmissionPacketId firstPacket = compiledGraph.packetIdAt(0u);
    const GpuSubmissionPacketId lastPacket = compiledGraph.packetIdAt(
        firstFrontierPacketIndex == packetCount ? packetCount - 1u : firstFrontierPacketIndex - 1u
    );
    outRange = compiledGraph.packetRange(firstPacket, lastPacket);
    return compiledGraph.validPacketRange(outRange);
}

[[nodiscard]] bool ValidateNormalGraphTaskStateBindings(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketRange& range,
    const GpuTaskPacketStateBinding* const taskStateBindings,
    const usize taskStateBindingCount
){
    if(!ValidateTaskPacketStateBindings(graph, compiledGraph, taskStateBindings, taskStateBindingCount))
        return false;

    const usize rangeEnd = static_cast<usize>(range.first.index) + range.packetCount;
    for(usize bindingIndex = 0u; bindingIndex < taskStateBindingCount; ++bindingIndex){
        const GpuSubmissionPacketId packet = compiledGraph.packetForTask(taskStateBindings[bindingIndex].task);
        if(
            !packet.valid()
            || packet.index < range.first.index
            || static_cast<usize>(packet.index) >= rangeEnd
        )
            return false;
    }
    return true;
}

[[nodiscard]] bool ValidateNormalGraphTaskRecordedCallbacks(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketRange& range,
    const GpuTaskGraphTaskRecordedCallback* const callbacks,
    const usize callbackCount
){
    if(callbackCount != 0u && !callbacks)
        return false;

    const usize rangeEnd = static_cast<usize>(range.first.index) + range.packetCount;
    for(usize callbackIndex = 0u; callbackIndex < callbackCount; ++callbackIndex){
        const GpuTaskGraphTaskRecordedCallback& callback = callbacks[callbackIndex];
        if(!callback.invoke || !graph.validTask(callback.task) || !compiledGraph.findTask(callback.task))
            return false;

        const GpuSubmissionPacketId packet = compiledGraph.packetForTask(callback.task);
        if(
            !packet.valid()
            || packet.index < range.first.index
            || static_cast<usize>(packet.index) >= rangeEnd
        )
            return false;

        for(usize previousIndex = 0u; previousIndex < callbackIndex; ++previousIndex){
            if(callbacks[previousIndex].task == callback.task)
                return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraphSubmitter::recordAndSubmitNormalGraph(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecorder& recorder,
    GpuRecordedGraph& recordedGraph,
    const GpuTaskGraphNormalExecutionDesc& desc,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    if(!compiledGraph.validFor(graph) || !transaction.validFor(compiledGraph))
        return false;

    GpuSubmissionPacketRange normalRange;
    GpuSubmissionPacketId failedPacket;
    if(!__hidden_gpu_packet_runtime_execution::FindNormalGraphPacketRange(
        graph,
        compiledGraph,
        desc.terminalTask,
        normalRange,
        &failedPacket
    )){
        if(outFailedPacket)
            *outFailedPacket = failedPacket;
        return false;
    }
    if(
        (desc.recordOverrideCount != 0u && !desc.recordOverrides)
        || (desc.externalCompletionTokenCount != 0u && !desc.externalCompletionTokens)
        || (desc.taskTimingTicketCount != 0u && !desc.taskTimingTickets)
        || (desc.submissionHookCount != 0u && !desc.submissionHooks)
        || (desc.taskAcceptedCallbackCount != 0u && !desc.taskAcceptedCallbacks)
        || (desc.taskSubmissionHookCount != 0u && !desc.taskSubmissionHooks)
        || (desc.acceptedCallback && !desc.acceptedCallback->invoke)
        || !__hidden_gpu_packet_runtime_execution::ValidateNormalGraphTaskStateBindings(
            graph,
            compiledGraph,
            normalRange,
            desc.taskStateBindings,
            desc.taskStateBindingCount
        )
        || !__hidden_gpu_packet_runtime_execution::ValidateNormalGraphTaskRecordedCallbacks(
            graph,
            compiledGraph,
            normalRange,
            desc.taskRecordedCallbacks,
            desc.taskRecordedCallbackCount
        )
    )
        return false;

    const bool recorded = desc.readyFrontierWorkerPool
        ? recorder.recordPacketRangeInReadyFrontiers(
            graph,
            compiledGraph,
            normalRange,
            desc.recordOverrides,
            desc.recordOverrideCount,
            recordedGraph,
            *desc.readyFrontierWorkerPool,
            &failedPacket,
            desc.commandIrCapture,
            desc.taskStateBindings,
            desc.taskStateBindingCount
        )
        : recorder.recordPacketRangeInCompileOrder(
            graph,
            compiledGraph,
            normalRange,
            desc.recordOverrides,
            desc.recordOverrideCount,
            recordedGraph,
            &failedPacket,
            desc.commandIrCapture,
            desc.taskStateBindings,
            desc.taskStateBindingCount
        )
    ;
    if(!recorded){
        if(outFailedPacket)
            *outFailedPacket = failedPacket;
        return false;
    }

    const usize normalRangeEnd = static_cast<usize>(normalRange.first.index) + normalRange.packetCount;
    for(usize packetIndex = normalRange.first.index; packetIndex < normalRangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
        const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
        const GpuTaskId* const tasks = compiledGraph.packetTasks(packet);
        if(!tasks){
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }

        for(u32 taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskId task = tasks[taskIndex];
            for(usize callbackIndex = 0u; callbackIndex < desc.taskRecordedCallbackCount; ++callbackIndex){
                const GpuTaskGraphTaskRecordedCallback& callback = desc.taskRecordedCallbacks[callbackIndex];
                if(callback.task != task)
                    continue;
                if(callback.invoke(callback.context, recordedGraph.taskFinalStateSeed(compiledGraph, task)))
                    continue;
                if(outFailedPacket)
                    *outFailedPacket = packet;
                return false;
            }
        }
    }

    if(!submitPacketRangeInCompileOrderFromTasks(
        graph,
        compiledGraph,
        recordedGraph,
        normalRange,
        desc.externalCompletionTokens,
        desc.externalCompletionTokenCount,
        desc.taskTimingTickets,
        desc.taskTimingTicketCount,
        transaction,
        scratchArena,
        &failedPacket,
        desc.acceptedCallback,
        desc.submissionHooks,
        desc.submissionHookCount,
        desc.taskAcceptedCallbacks,
        desc.taskAcceptedCallbackCount,
        desc.taskSubmissionHooks,
        desc.taskSubmissionHookCount
    )){
        if(outFailedPacket)
            *outFailedPacket = failedPacket;
        return false;
    }
    return true;
}


bool GpuTaskGraphSubmitter::recordAndSubmitPacketRangeInCompileOrder(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecorder& recorder,
    GpuRecordedGraph& recordedGraph,
    const GpuSubmissionPacketRange& range,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    if(
        !compiledGraph.validFor(graph)
        || !transaction.validFor(compiledGraph)
        || !compiledGraph.validPacketRange(range)
    )
        return false;

    const usize rangeEnd = static_cast<usize>(range.first.index) + range.packetCount;
    for(usize packetIndex = range.first.index; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
        if(!compiledGraph.packet(packet).joinsAcceptedQueueFrontier)
            continue;
        if(outFailedPacket)
            *outFailedPacket = packet;
        return false;
    }

    GpuSubmissionPacketId failedPacket;
    if(!recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        range,
        nullptr,
        0u,
        recordedGraph,
        &failedPacket
    )){
        if(outFailedPacket)
            *outFailedPacket = failedPacket;
        return false;
    }
    if(!submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        range,
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena,
        &failedPacket
    )){
        if(outFailedPacket)
            *outFailedPacket = failedPacket;
        return false;
    }
    return true;
}


bool GpuTaskGraphSubmitter::recordAndSubmitTaskRangeInCompileOrder(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecorder& recorder,
    GpuRecordedGraph& recordedGraph,
    const GpuTaskId firstTask,
    const GpuTaskId lastTask,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    return recordAndSubmitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        compiledGraph.packetRangeForTasks(firstTask, lastTask),
        transaction,
        scratchArena,
        outFailedPacket
    );
}


bool GpuTaskGraphSubmitter::recordAndSubmitPacketRangeInReadyFrontiers(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecorder& recorder,
    GpuRecordedGraph& recordedGraph,
    Alloc::ThreadPool& workerPool,
    const GpuSubmissionPacketRange& range,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    if(
        !compiledGraph.validFor(graph)
        || !transaction.validFor(compiledGraph)
        || !compiledGraph.validPacketRange(range)
    )
        return false;

    const usize rangeEnd = static_cast<usize>(range.first.index) + range.packetCount;
    for(usize packetIndex = range.first.index; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
        if(!compiledGraph.packet(packet).joinsAcceptedQueueFrontier)
            continue;
        if(outFailedPacket)
            *outFailedPacket = packet;
        return false;
    }

    GpuSubmissionPacketId failedPacket;
    if(!recorder.recordPacketRangeInReadyFrontiers(
        graph,
        compiledGraph,
        range,
        nullptr,
        0u,
        recordedGraph,
        workerPool,
        &failedPacket
    )){
        if(outFailedPacket)
            *outFailedPacket = failedPacket;
        return false;
    }
    if(!submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        range,
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena,
        &failedPacket
    )){
        if(outFailedPacket)
            *outFailedPacket = failedPacket;
        return false;
    }
    return true;
}


bool GpuTaskGraphSubmitter::recordAndSubmitTaskRangeInReadyFrontiers(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecorder& recorder,
    GpuRecordedGraph& recordedGraph,
    Alloc::ThreadPool& workerPool,
    const GpuTaskId firstTask,
    const GpuTaskId lastTask,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    return recordAndSubmitPacketRangeInReadyFrontiers(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        workerPool,
        compiledGraph.packetRangeForTasks(firstTask, lastTask),
        transaction,
        scratchArena,
        outFailedPacket
    );
}


bool GpuTaskGraphSubmitter::recordAndSubmitAcceptedFrontierTask(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecorder& recorder,
    GpuRecordedGraph& recordedGraph,
    const GpuTaskId task,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(outFailedPacket)
        *outFailedPacket = {};

    const auto rejectTask = [&]{
        if(
            compiledGraph.validFor(graph)
            && transaction.validFor(compiledGraph)
            && graph.validTask(task)
            && compiledGraph.findTask(task)
        ){
            u64 recordingAttemptGeneration = recordedGraph.recordingAttemptGeneration();
            if(recordingAttemptGeneration == 0u){
                const GpuSubmissionPacketId packet = compiledGraph.packetForTask(task);
                if(!graph.beginRecordingAttempt(compiledGraph, packet))
                    return;
                recordingAttemptGeneration = graph.recordingAttemptGeneration();
            }
            transaction.rejectTask(
                graph,
                compiledGraph,
                task,
                recordingAttemptGeneration
            );
        }
    };
    if(
        !compiledGraph.validFor(graph)
        || !transaction.validFor(compiledGraph)
        || !graph.validTask(task)
        || !compiledGraph.findTask(task)
        || !compiledGraph.taskJoinsAcceptedQueueFrontier(task)
        || !transaction.waitForSubmissionPublicationAndHasAcceptedPackets()
    ){
        rejectTask();
        return false;
    }

    return recordAndSubmitTask(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        task,
        nullptr,
        0u,
        nullptr,
        transaction,
        scratchArena,
        outFailedPacket
    );
}


bool GpuTaskGraphSubmitter::recordAndSubmitTask(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecorder& recorder,
    GpuRecordedGraph& recordedGraph,
    const GpuTaskId task,
    const GpuTaskPacketStateBinding* const taskStateBindings,
    const usize taskStateBindingCount,
    const GpuTaskGraphTaskRecordedCallback* const recordedCallback,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket,
    const GpuTaskGraphTaskAcceptedCallback* const acceptedCallback
)const{
    if(outFailedPacket)
        *outFailedPacket = {};

    const auto rejectTask = [&]{
        if(
            compiledGraph.validFor(graph)
            && transaction.validFor(compiledGraph)
            && graph.validTask(task)
            && compiledGraph.findTask(task)
        ){
            u64 recordingAttemptGeneration = recordedGraph.recordingAttemptGeneration();
            if(recordingAttemptGeneration == 0u){
                const GpuSubmissionPacketId packet = compiledGraph.packetForTask(task);
                if(!graph.beginRecordingAttempt(compiledGraph, packet))
                    return;
                recordingAttemptGeneration = graph.recordingAttemptGeneration();
            }
            transaction.rejectTask(
                graph,
                compiledGraph,
                task,
                recordingAttemptGeneration
            );
        }
    };
    if(
        !compiledGraph.validFor(graph)
        || !transaction.validFor(compiledGraph)
        || !graph.validTask(task)
        || !compiledGraph.findTask(task)
        || (taskStateBindingCount != 0u && !taskStateBindings)
        || (recordedCallback && (!recordedCallback->invoke || recordedCallback->task != task))
        || (acceptedCallback && (!acceptedCallback->invoke || acceptedCallback->task != task))
    ){
        rejectTask();
        return false;
    }

    GpuSubmissionPacketId failedPacket;
    if(!recorder.recordTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        task,
        task,
        nullptr,
        0u,
        recordedGraph,
        &failedPacket,
        nullptr,
        taskStateBindings,
        taskStateBindingCount
    )){
        if(outFailedPacket)
            *outFailedPacket = failedPacket;
        rejectTask();
        return false;
    }

    if(
        recordedCallback
        && !recordedCallback->invoke(
            recordedCallback->context,
            recordedGraph.taskFinalStateSeed(compiledGraph, task)
        )
    ){
        if(outFailedPacket)
            *outFailedPacket = compiledGraph.packetForTask(task);
        rejectTask();
        return false;
    }

    if(!submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        task,
        task,
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena,
        &failedPacket,
        nullptr,
        nullptr,
        0u,
        acceptedCallback,
        acceptedCallback ? 1u : 0u
    )){
        if(outFailedPacket)
            *outFailedPacket = failedPacket;
        // submitPacket() normally rejected the packet already. Keep this idempotent closeout for validation
        // failures that happen before packet traversal, so a renderer cannot strand an armed recovery task.
        rejectTask();
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

