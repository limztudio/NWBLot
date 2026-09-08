// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_execution{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A frontier packet is meaningful only as an explicit recovery/finalization tail. The compiler prevents it from
// merging with ordinary work, but does not force declaration order, so normal graph execution rejects one inside
// either its semantic endpoint prefix or its automatically derived ordinary prefix before recording begins.
[[nodiscard]] bool FindNormalGraphPacketRange(
    const GpuTaskGraph::DeclarationReadView& declarations,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuTaskId& terminalTask,
    GpuSubmissionPacketRange& outRange,
    GpuSubmissionPacketId* const outFailedPacket
){
    outRange = {};
    if(outFailedPacket)
        *outFailedPacket = {};

    const usize packetCount = planAccess.packetCount();
    if(packetCount == 0u)
        return false;

    if(terminalTask.valid()){
        if(!declarations.validTask(terminalTask) || !planAccess.findTask(terminalTask).valid())
            return false;
        const GpuSubmissionPacketId terminalPacket = planAccess.packetForTask(terminalTask);
        if(!terminalPacket.valid())
            return false;
        for(usize packetIndex = 0u; packetIndex <= terminalPacket.index; ++packetIndex){
            const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
            const GpuCompiledPacketView packetView = planAccess.packet(packet);
            if(!packetView.valid())
                return false;
            if(!packetView.plan->joinsAcceptedQueueFrontier)
                continue;
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }

        outRange = planAccess.packetRange(planAccess.packetIdAt(0u), terminalPacket);
        return planAccess.validPacketRange(outRange);
    }

    usize firstFrontierPacketIndex = packetCount;
    for(usize packetIndex = 0u; packetIndex < packetCount; ++packetIndex){
        const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
        const GpuCompiledPacketView packetView = planAccess.packet(packet);
        if(!packetView.valid())
            return false;
        if(packetView.plan->joinsAcceptedQueueFrontier){
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
            *outFailedPacket = planAccess.packetIdAt(0u);
        return false;
    }

    const GpuSubmissionPacketId firstPacket = planAccess.packetIdAt(0u);
    const GpuSubmissionPacketId lastPacket = planAccess.packetIdAt(
        firstFrontierPacketIndex == packetCount ? packetCount - 1u : firstFrontierPacketIndex - 1u
    );
    outRange = planAccess.packetRange(firstPacket, lastPacket);
    return planAccess.validPacketRange(outRange);
}

[[nodiscard]] bool ValidateNormalGraphTaskRecordedCallbacks(
    const GpuTaskGraph::DeclarationReadView& declarations,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketRange& range,
    const GpuTaskGraphTaskRecordedCallback* const callbacks,
    const usize callbackCount
){
    if(callbackCount != 0u && !callbacks)
        return false;

    const usize rangeEnd = static_cast<usize>(range.first.index) + range.packetCount;
    for(usize callbackIndex = 0u; callbackIndex < callbackCount; ++callbackIndex){
        const GpuTaskGraphTaskRecordedCallback& callback = callbacks[callbackIndex];
        if(!callback.invoke || !declarations.validTask(callback.task) || !planAccess.findTask(callback.task).valid())
            return false;

        const GpuSubmissionPacketId packet = planAccess.packetForTask(callback.task);
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


bool GpuTaskScheduler::recordAndSubmitNormalGraph(
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
    SubmissionAttemptExceptionFinalizer exceptionFinalizer(graph, compiledGraph, recordedGraph, transaction);
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;
    GpuRecordedGraph::ArtifactOperation artifactOperation(
        recordedGraph,
        GpuRecordedGraph::ArtifactOperationMode::Exclusive
    );
    if(!artifactOperation.valid())
        return false;
    GpuGraphSubmissionTransaction::SubmissionOperation submissionOperation(
        transaction,
        GpuGraphSubmissionTransaction::SubmissionOperationMode::CompositeBarrier,
        &artifactOperation
    );
    if(!submissionOperation.valid())
        return false;
    SubmissionAttemptExceptionScope exceptionScope(graph, compiledGraph, recordedGraph, transaction, outFailedPacket);

    GpuSubmissionPacketRange normalRange;
    GpuSubmissionPacketId failedPacket;
    {
        GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::tryAcquire(graph);
        if(
            !declarationAccess.valid()
            || !planAccess.validFor(declarationAccess)
            || !transaction.validFor(planAccess)
        )
            return false;
        if(!__hidden_gpu_packet_runtime_execution::FindNormalGraphPacketRange(
            declarationAccess,
            planAccess,
            desc.terminalTask,
            normalRange,
            &failedPacket
        )){
            if(outFailedPacket)
                *outFailedPacket = failedPacket;
            return false;
        }
        exceptionScope.setFailedPacket(normalRange.first);
        if(
            (desc.externalCompletionTokenCount != 0u && !desc.externalCompletionTokens)
            || (desc.taskTimingTicketCount != 0u && !desc.taskTimingTickets)
            || (desc.taskAcceptedCallbackCount != 0u && !desc.taskAcceptedCallbacks)
            || (desc.taskSubmissionHookCount != 0u && !desc.taskSubmissionHooks)
            || !__hidden_gpu_packet_runtime_execution::ValidateNormalGraphTaskRecordedCallbacks(
                declarationAccess,
                planAccess,
                normalRange,
                desc.taskRecordedCallbacks,
                desc.taskRecordedCallbackCount
            )
        )
            return false;

        if(!prepareRecordingAttemptAndBindTransactionWithinSubmissionOperation(
            graph,
            compiledGraph,
            normalRange.first,
            recorder.m_timingRecorder,
            recordedGraph,
            transaction,
            artifactOperation,
            declarationAccess,
            planAccess
        ))
            return false;
    }

    if(!recordPacketRange(
        graph,
        compiledGraph,
        recorder,
        normalRange,
        recordedGraph,
        desc.readyFrontierScheduler,
        desc.commandIrCapture,
        &failedPacket
    )){
        if(outFailedPacket)
            *outFailedPacket = failedPacket;
        return false;
    }

    const usize normalRangeEnd = static_cast<usize>(normalRange.first.index) + normalRange.packetCount;
    for(usize packetIndex = normalRange.first.index; packetIndex < normalRangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
        exceptionScope.setFailedPacket(packet);
        const GpuCompiledPacketView packetView = planAccess.packet(packet);
        if(!packetView.valid()){
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }
        const GpuSubmissionPacket& packetPlan = *packetView.plan;
        const GpuTaskId* const tasks = packetView.tasks;

        for(u32 taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskId task = tasks[taskIndex];
            for(usize callbackIndex = 0u; callbackIndex < desc.taskRecordedCallbackCount; ++callbackIndex){
                const GpuTaskGraphTaskRecordedCallback& callback = desc.taskRecordedCallbacks[callbackIndex];
                if(callback.task != task)
                    continue;
                if(callback.invoke(
                    callback.context,
                    recordedGraph.packetStateSeed(packet, artifactOperation)
                ))
                    continue;
                if(outFailedPacket)
                    *outFailedPacket = packet;
                return false;
            }
        }
    }

    if(!submitPacketRangeInCompileOrderWithOperationPolicy(
        graph,
        compiledGraph,
        recordedGraph,
        normalRange,
        PacketRangeSubmissionOperationPolicy::ActiveExclusiveBarrier,
        desc.externalCompletionTokens,
        desc.externalCompletionTokenCount,
        desc.taskTimingTickets,
        desc.taskTimingTicketCount,
        transaction,
        scratchArena,
        &failedPacket,
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


bool GpuTaskScheduler::recordPacketRange(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecorder& recorder,
    const GpuSubmissionPacketRange& range,
    GpuRecordedGraph& recordedGraph,
    CpuTaskScheduler* const readyFrontierScheduler,
    GpuCommandIrCapture* const commandIrCapture,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    return readyFrontierScheduler
        ? recorder.recordPacketRangeInReadyFrontiers(
            graph,
            compiledGraph,
            range,
            recordedGraph,
            *readyFrontierScheduler,
            outFailedPacket,
            commandIrCapture
        )
        : recorder.recordPacketRangeInCompileOrder(
            graph,
            compiledGraph,
            range,
            recordedGraph,
            outFailedPacket,
            commandIrCapture
        )
    ;
}


bool GpuTaskScheduler::recordAndSubmitTaskRangeInCompileOrder(
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
    return recordAndSubmitTaskRange(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        nullptr,
        firstTask,
        lastTask,
        transaction,
        scratchArena,
        outFailedPacket
    );
}


bool GpuTaskScheduler::recordAndSubmitTaskRangeInReadyFrontiers(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecorder& recorder,
    GpuRecordedGraph& recordedGraph,
    CpuTaskScheduler& cpuScheduler,
    const GpuTaskId firstTask,
    const GpuTaskId lastTask,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    return recordAndSubmitTaskRange(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        &cpuScheduler,
        firstTask,
        lastTask,
        transaction,
        scratchArena,
        outFailedPacket
    );
}


bool GpuTaskScheduler::recordAndSubmitTaskRange(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecorder& recorder,
    GpuRecordedGraph& recordedGraph,
    CpuTaskScheduler* const readyFrontierScheduler,
    const GpuTaskId firstTask,
    const GpuTaskId lastTask,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    SubmissionAttemptExceptionFinalizer exceptionFinalizer(graph, compiledGraph, recordedGraph, transaction);
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;
    GpuRecordedGraph::ArtifactOperation artifactOperation(
        recordedGraph,
        GpuRecordedGraph::ArtifactOperationMode::Exclusive
    );
    if(!artifactOperation.valid())
        return false;
    GpuGraphSubmissionTransaction::SubmissionOperation submissionOperation(
        transaction,
        GpuGraphSubmissionTransaction::SubmissionOperationMode::CompositeBarrier,
        &artifactOperation
    );
    if(!submissionOperation.valid())
        return false;
    SubmissionAttemptExceptionScope exceptionScope(graph, compiledGraph, recordedGraph, transaction, outFailedPacket);

    GpuSubmissionPacketRange range;
    {
        GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::tryAcquire(graph);
        if(!declarationAccess.valid())
            return false;
        range = planAccess.packetRangeForTasks(firstTask, lastTask);
        if(
            !planAccess.validFor(declarationAccess)
            || !transaction.validFor(planAccess)
            || !planAccess.validPacketRange(range)
        )
            return false;
        exceptionScope.setFailedPacket(range.first);

        const usize rangeEnd = static_cast<usize>(range.first.index) + range.packetCount;
        for(usize packetIndex = range.first.index; packetIndex < rangeEnd; ++packetIndex){
            const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
            const GpuCompiledPacketView packetView = planAccess.packet(packet);
            if(!packetView.valid())
                return false;
            if(!packetView.plan->joinsAcceptedQueueFrontier)
                continue;
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }

        if(!prepareRecordingAttemptAndBindTransactionWithinSubmissionOperation(
            graph,
            compiledGraph,
            range.first,
            recorder.m_timingRecorder,
            recordedGraph,
            transaction,
            artifactOperation,
            declarationAccess,
            planAccess
        ))
            return false;
    }

    GpuSubmissionPacketId failedPacket;
    if(!recordPacketRange(
        graph,
        compiledGraph,
        recorder,
        range,
        recordedGraph,
        readyFrontierScheduler,
        nullptr,
        &failedPacket
    )){
        if(outFailedPacket)
            *outFailedPacket = failedPacket;
        return false;
    }
    if(!submitPacketRangeInCompileOrderWithOperationPolicy(
        graph,
        compiledGraph,
        recordedGraph,
        range,
        PacketRangeSubmissionOperationPolicy::ActiveExclusiveBarrier,
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena,
        &failedPacket,
        nullptr,
        0u,
        nullptr,
        0u
    )){
        if(outFailedPacket)
            *outFailedPacket = failedPacket;
        return false;
    }
    return true;
}


bool GpuTaskScheduler::recordAndSubmitAcceptedFrontierTask(
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
    SubmissionAttemptExceptionFinalizer exceptionFinalizer(graph, compiledGraph, recordedGraph, transaction);
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;
    GpuRecordedGraph::ArtifactOperation artifactOperation(
        recordedGraph,
        GpuRecordedGraph::ArtifactOperationMode::Exclusive
    );
    if(!artifactOperation.valid())
        return false;
    GpuGraphSubmissionTransaction::SubmissionOperation submissionOperation(
        transaction,
        GpuGraphSubmissionTransaction::SubmissionOperationMode::CompositeBarrier,
        &artifactOperation
    );
    if(!submissionOperation.valid())
        return false;
    SubmissionAttemptExceptionScope exceptionScope(graph, compiledGraph, recordedGraph, transaction, outFailedPacket);


    const auto rejectTask = [&](const GpuTaskGraph::DeclarationReadView& declarationAccess){
        if(
            planAccess.validFor(declarationAccess)
            && transaction.validFor(planAccess)
            && declarationAccess.validTask(task)
            && planAccess.findTask(task).valid()
        ){
            u64 recordingAttemptGeneration = recordedGraph.recordingAttemptGeneration();
            if(recordingAttemptGeneration == 0u){
                const GpuSubmissionPacketId packet = planAccess.packetForTask(task);
                GpuTaskGraph::RecordingAttemptScope provisionalAttempt;
                if(!graph.beginRecordingAttempt(
                    compiledGraph,
                    packet,
                    declarationAccess,
                    planAccess,
                    provisionalAttempt
                ))
                    return;
                recordingAttemptGeneration = graph.recordingAttemptGeneration();
                if(!transaction.bindRecordingAttemptWithinSubmissionOperation(
                    graph,
                    compiledGraph,
                    recordingAttemptGeneration,
                    &provisionalAttempt
                ))
                    return;
                provisionalAttempt.complete();
            }
            transaction.rejectTaskWithinSubmissionOperation(
                graph,
                declarationAccess,
                compiledGraph,
                planAccess,
                task,
                recordingAttemptGeneration
            );
        }
    };
    GpuSubmissionPacketId packet;
    {
        GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::tryAcquire(graph);
        if(!declarationAccess.valid())
            return false;
        packet = planAccess.packetForTask(task);
        exceptionScope.setFailedPacket(packet);
        if(
            !planAccess.validFor(declarationAccess)
            || !transaction.validFor(planAccess)
            || !declarationAccess.validTask(task)
            || !planAccess.findTask(task).valid()
            || !planAccess.taskJoinsAcceptedQueueFrontier(task)
        ){
            rejectTask(declarationAccess);
            return false;
        }
        if(!prepareRecordingAttemptAndBindTransactionWithinSubmissionOperation(
            graph,
            compiledGraph,
            packet,
            recorder.m_timingRecorder,
            recordedGraph,
            transaction,
            artifactOperation,
            declarationAccess,
            planAccess
        ))
            return false;
    }
    if(!transaction.waitForSubmissionPublicationAndHasAcceptedPacketsWithinSubmissionOperation()){
        if(outFailedPacket)
            *outFailedPacket = packet;
        GpuTaskGraph::DeclarationReadView rejectionDeclarations = GpuTaskGraph::DeclarationReadView::tryAcquire(graph);
        rejectTask(rejectionDeclarations);
        return false;
    }

    return recordAndSubmitTaskWithinSubmissionOperation(
        graph,
        compiledGraph,
        planAccess,
        artifactOperation,
        recorder,
        recordedGraph,
        task,
        nullptr,
        transaction,
        scratchArena,
        outFailedPacket,
        nullptr
    );
}


bool GpuTaskScheduler::recordAndSubmitTask(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecorder& recorder,
    GpuRecordedGraph& recordedGraph,
    const GpuTaskId task,
    const GpuTaskGraphTaskRecordedCallback* const recordedCallback,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket,
    const GpuTaskGraphTaskAcceptedCallback* const acceptedCallback
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    SubmissionAttemptExceptionFinalizer exceptionFinalizer(graph, compiledGraph, recordedGraph, transaction);
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;
    GpuRecordedGraph::ArtifactOperation artifactOperation(
        recordedGraph,
        GpuRecordedGraph::ArtifactOperationMode::Exclusive
    );
    if(!artifactOperation.valid())
        return false;
    GpuGraphSubmissionTransaction::SubmissionOperation submissionOperation(
        transaction,
        GpuGraphSubmissionTransaction::SubmissionOperationMode::CompositeBarrier,
        &artifactOperation
    );
    if(!submissionOperation.valid())
        return false;
    SubmissionAttemptExceptionScope exceptionScope(graph, compiledGraph, recordedGraph, transaction, outFailedPacket);

    {
        GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::tryAcquire(graph);
        if(
            !declarationAccess.valid()
            || !planAccess.validFor(declarationAccess)
            || !transaction.validFor(planAccess)
            || !declarationAccess.validTask(task)
            || !planAccess.findTask(task).valid()
        )
            return false;

        const GpuSubmissionPacketId packet = planAccess.packetForTask(task);
        exceptionScope.setFailedPacket(packet);
        if(!prepareRecordingAttemptAndBindTransactionWithinSubmissionOperation(
            graph,
            compiledGraph,
            packet,
            recorder.m_timingRecorder,
            recordedGraph,
            transaction,
            artifactOperation,
            declarationAccess,
            planAccess
        ))
            return false;
    }

    return recordAndSubmitTaskWithinSubmissionOperation(
        graph,
        compiledGraph,
        planAccess,
        artifactOperation,
        recorder,
        recordedGraph,
        task,
        recordedCallback,
        transaction,
        scratchArena,
        outFailedPacket,
        acceptedCallback
    );
}


bool GpuTaskScheduler::recordAndSubmitTaskWithinSubmissionOperation(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuRecordedGraph::ArtifactOperation& artifactAccess,
    const GpuNativePacketRecorder& recorder,
    GpuRecordedGraph& recordedGraph,
    const GpuTaskId task,
    const GpuTaskGraphTaskRecordedCallback* const recordedCallback,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket,
    const GpuTaskGraphTaskAcceptedCallback* const acceptedCallback
)const{
    if(
        !planAccess.validFor(compiledGraph)
        || !artifactAccess.exclusiveFor(recordedGraph)
        || !GpuGraphSubmissionTransaction::SubmissionOperation::activeExclusiveFor(transaction)
    )
        return false;

    GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::tryAcquire(graph);
    if(
        !declarationAccess.valid()
        || !planAccess.validFor(declarationAccess)
        || !transaction.validFor(planAccess)
        || !declarationAccess.validTask(task)
        || !planAccess.findTask(task).valid()
    )
        return false;

    const u64 recordingAttemptGeneration = recordedGraph.recordingAttemptGeneration();
    if(
        recordingAttemptGeneration == 0u
        || !graph.matchesRecordingAttempt(compiledGraph, recordingAttemptGeneration)
    )
        return false;

    const auto rejectTask = [&]{
        transaction.rejectTaskWithinSubmissionOperation(
            graph,
            declarationAccess,
            compiledGraph,
            planAccess,
            task,
            recordingAttemptGeneration
        );
    };
    if(
        (recordedCallback && (!recordedCallback->invoke || recordedCallback->task != task))
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
        recordedGraph,
        &failedPacket,
        nullptr
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
            recordedGraph.packetStateSeed(planAccess.packetForTask(task), artifactAccess)
        )
    ){
        if(outFailedPacket)
            *outFailedPacket = planAccess.packetForTask(task);
        rejectTask();
        return false;
    }

    if(!submitPacketRangeInCompileOrderWithOperationPolicy(
        graph,
        compiledGraph,
        recordedGraph,
        planAccess.packetRangeForTasks(task, task),
        PacketRangeSubmissionOperationPolicy::ActiveExclusiveBarrier,
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena,
        &failedPacket,
        acceptedCallback,
        acceptedCallback ? 1u : 0u,
        nullptr,
        0u
    )){
        if(outFailedPacket)
            *outFailedPacket = failedPacket;
        // Range submission normally rejected the packet already. Keep this idempotent closeout for validation
        // failures that happen before packet traversal, so a renderer cannot strand an armed recovery task.
        rejectTask();
        return false;
    }
    return true;
}


bool GpuTaskScheduler::prepareRecordingAttemptAndBindTransactionWithinSubmissionOperation(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    GpuTimingRecorder* const timingRecorder,
    GpuRecordedGraph& recordedGraph,
    GpuGraphSubmissionTransaction& transaction,
    const GpuRecordedGraph::ArtifactOperation& artifactAccess,
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph::ReadView& planAccess
)const{
    if(
        !planAccess.validFor(compiledGraph)
        || !artifactAccess.exclusiveFor(recordedGraph)
        || !GpuGraphSubmissionTransaction::SubmissionOperation::activeExclusiveFor(transaction)
        || !declarationAccess.validFor(graph)
        || !planAccess.validFor(declarationAccess)
        || !planAccess.validPacket(packet)
        || !transaction.validFor(planAccess)
    )
        return false;
    if(recordedGraph.validForWithinArtifactOperation(
        graph,
        declarationAccess,
        compiledGraph,
        planAccess,
        artifactAccess
    )){
        GpuTimingRecorder* const artifactTimingRecorder =
            recordedGraph.timingRecorderWithinArtifactOperation(artifactAccess)
        ;
        if(artifactTimingRecorder && artifactTimingRecorder != timingRecorder)
            return false;
        return transaction.bindRecordingAttemptWithinSubmissionOperation(
            graph,
            compiledGraph,
            recordedGraph.recordingAttemptGenerationWithinArtifactOperation(artifactAccess)
        );
    }
    if(!recordedGraph.prepareRecordingStorageCandidate(compiledGraph, planAccess, timingRecorder))
        return false;

    GpuTaskGraph::RecordingAttemptScope provisionalAttempt;
    if(!graph.beginRecordingAttempt(compiledGraph, packet, declarationAccess, planAccess, provisionalAttempt))
        return false;
    if(!provisionalAttempt.m_graph)
        return false;

    if(!transaction.bindRecordingAttemptWithinSubmissionOperation(
        graph,
        compiledGraph,
        provisionalAttempt.m_recordingAttemptGeneration,
        &provisionalAttempt
    ))
        return false;
    recordedGraph.publishStorageCandidate(
        &graph,
        compiledGraph,
        planAccess,
        provisionalAttempt.m_recordingAttemptGeneration,
        artifactAccess
    );
    provisionalAttempt.complete();
    return recordedGraph.validForWithinArtifactOperation(
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

