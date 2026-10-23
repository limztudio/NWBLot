// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"

#include "packet_runtime_internal.h"
#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_scheduler{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A frontier packet is only a recovery/finalization tail. The compiler keeps it out of ordinary work, but does not force declaration order, so execution rejects one inside its endpoint.
[[nodiscard]] Expected<GpuSubmissionPacketRange, GpuSubmissionPacketId> FindNormalGraphPacketRange(const GpuTaskGraph::DeclarationReadView& declarations, const GpuCompiledGraph::ReadView& planAccess, const GpuTaskId& terminalTask)noexcept{
    const usize packetCount = planAccess.packetCount();
    if(packetCount == 0u)
        return MakeUnexpected(GpuSubmissionPacketId{});

    if(terminalTask.valid()){
        if(!declarations.validTask(terminalTask) || !planAccess.findTask(terminalTask).valid())
            return MakeUnexpected(GpuSubmissionPacketId{});
        const GpuSubmissionPacketId terminalPacket = planAccess.packetForTask(terminalTask);
        if(!terminalPacket.valid())
            return MakeUnexpected(GpuSubmissionPacketId{});
        for(usize packetIndex = 0u; packetIndex <= terminalPacket.index; ++packetIndex){
            const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
            const GpuCompiledPacketView packetView = planAccess.packet(packet);
            if(!packetView.valid())
                return MakeUnexpected(GpuSubmissionPacketId{});
            if(!packetView.plan->joinsAcceptedQueueFrontier)
                continue;
            return MakeUnexpected(packet);
        }

        const GpuSubmissionPacketRange range = planAccess.packetRange(planAccess.packetIdAt(0u), terminalPacket);
        if(!planAccess.validPacketRange(range))
            return MakeUnexpected(GpuSubmissionPacketId{});
        return range;
    }

    usize firstFrontierPacketIndex = packetCount;
    for(usize packetIndex = 0u; packetIndex < packetCount; ++packetIndex){
        const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
        const GpuCompiledPacketView packetView = planAccess.packet(packet);
        if(!packetView.valid())
            return MakeUnexpected(GpuSubmissionPacketId{});
        if(packetView.plan->joinsAcceptedQueueFrontier){
            if(firstFrontierPacketIndex == packetCount)
                firstFrontierPacketIndex = packetIndex;
            continue;
        }
        if(firstFrontierPacketIndex != packetCount){
            return MakeUnexpected(packet);
        }
    }

    if(firstFrontierPacketIndex == 0u){
        return MakeUnexpected(planAccess.packetIdAt(0u));
    }

    const GpuSubmissionPacketId firstPacket = planAccess.packetIdAt(0u);
    const GpuSubmissionPacketId lastPacket = planAccess.packetIdAt(firstFrontierPacketIndex == packetCount ? packetCount - 1u : firstFrontierPacketIndex - 1u);
    const GpuSubmissionPacketRange range = planAccess.packetRange(firstPacket, lastPacket);
    if(!planAccess.validPacketRange(range))
        return MakeUnexpected(GpuSubmissionPacketId{});
    return range;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskScheduler::submitGraph(
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecorder& recorder,
    const GpuTaskGraphNormalExecutionDesc& desc,
    Alloc::ScratchArena& scratchArena,
    GpuTaskGraph& graph,
    GpuRecordedGraph& recordedGraph,
    GpuGraphSubmissionTransaction& transaction,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    DeviceOperation deviceOperation(*this);
    if(!deviceOperation.m_admitted || &recorder.m_device != &device())
        return false;

    SubmissionAttemptExceptionFinalizer exceptionFinalizer(graph, transaction, compiledGraph, recordedGraph);
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;
    GpuRecordedGraph::ArtifactOperation artifactOperation(recordedGraph, GpuRecordedGraph::ArtifactOperationMode::Exclusive);
    if(!artifactOperation.valid())
        return false;
    GpuGraphSubmissionTransaction::SubmissionOperation submissionOperation(transaction, GpuGraphSubmissionTransaction::SubmissionOperationMode::CompositeBarrier, &artifactOperation);
    if(!submissionOperation.valid())
        return false;
    SubmissionAttemptExceptionScope exceptionScope(compiledGraph, recordedGraph, graph, transaction, outFailedPacket);

    GpuTaskSubmissionDetail::TaskCallbackBindings<GpuTaskGraphTaskRecordedCallback> recordedCallbacks(scratchArena);
    GpuSubmissionPacketRange normalRange;
    GpuSubmissionPacketId failedPacket;
    {
        GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::TryAcquire(graph);
        if(!declarationAccess.valid() || !planAccess.validFor(declarationAccess) || !transaction.validFor(planAccess))
            return false;
        const auto range = __hidden_gpu_task_scheduler::FindNormalGraphPacketRange(declarationAccess, planAccess, desc.terminalTask);
        if(!range){
            if(outFailedPacket)
                *outFailedPacket = range.error();
            return false;
        }
        normalRange = *range;
        exceptionScope.setFailedPacket(normalRange.first);
        if(
            (desc.taskTimingTicketCount != 0u && !desc.taskTimingTickets)
            || (desc.taskAcceptedCallbackCount != 0u && !desc.taskAcceptedCallbacks)
            || (desc.taskSubmissionHookCount != 0u && !desc.taskSubmissionHooks)
            || !recordedCallbacks.resolve(declarationAccess, planAccess, normalRange, desc.taskRecordedCallbacks, desc.taskRecordedCallbackCount)
        )
            return false;

        if(!prepareRecordingAttemptAndBindTransactionWithinSubmissionOperation(compiledGraph, normalRange.first, artifactOperation, declarationAccess, planAccess, graph, recorder.m_timingRecorder, recordedGraph, transaction))
            return false;
    }

    if(!recordPacketRange(graph, compiledGraph, recorder, normalRange, desc.readyFrontierScheduler, scratchArena, recordedGraph, desc.commandIrCapture, &failedPacket)){
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
            const GpuTaskGraphTaskRecordedCallback* const callback = recordedCallbacks.find(task);
            if(!callback || callback->invoke(callback->context, recordedGraph.packetStateSeed(packet, artifactOperation)))
                continue;
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }
    }

    if(!submitPacketRangeWithinSubmissionOperation(
        compiledGraph,
        recordedGraph,
        normalRange,
        desc.taskTimingTickets,
        desc.taskTimingTicketCount,
        desc.taskAcceptedCallbacks,
        desc.taskAcceptedCallbackCount,
        desc.taskSubmissionHooks,
        desc.taskSubmissionHookCount,
        scratchArena,
        graph,
        transaction,
        &failedPacket
    )){
        if(outFailedPacket)
            *outFailedPacket = failedPacket;
        return false;
    }
    return true;
}


bool GpuTaskScheduler::recordAndSubmitAcceptedFrontierTask(
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecorder& recorder,
    const GpuTaskId task,
    Alloc::ScratchArena& scratchArena,
    GpuTaskGraph& graph,
    GpuRecordedGraph& recordedGraph,
    GpuGraphSubmissionTransaction& transaction,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    DeviceOperation deviceOperation(*this);
    if(!deviceOperation.m_admitted || &recorder.m_device != &device())
        return false;

    SubmissionAttemptExceptionFinalizer exceptionFinalizer(graph, transaction, compiledGraph, recordedGraph);
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;
    GpuRecordedGraph::ArtifactOperation artifactOperation(recordedGraph, GpuRecordedGraph::ArtifactOperationMode::Exclusive);
    if(!artifactOperation.valid())
        return false;
    GpuGraphSubmissionTransaction::SubmissionOperation submissionOperation(transaction, GpuGraphSubmissionTransaction::SubmissionOperationMode::CompositeBarrier, &artifactOperation);
    if(!submissionOperation.valid())
        return false;
    SubmissionAttemptExceptionScope exceptionScope(compiledGraph, recordedGraph, graph, transaction, outFailedPacket);


    const auto rejectTask = [&](const GpuTaskGraph::DeclarationReadView& declarationAccess){
        if(planAccess.validFor(declarationAccess) && transaction.validFor(planAccess) && declarationAccess.validTask(task) && planAccess.findTask(task).valid()){
            u64 recordingAttemptGeneration = recordedGraph.recordingAttemptGeneration();
            if(recordingAttemptGeneration == 0u){
                const GpuSubmissionPacketId packet = planAccess.packetForTask(task);
                GpuRecordingAttemptScope provisionalAttempt;
                if(!graph.beginRecordingAttempt(compiledGraph, packet, declarationAccess, planAccess, provisionalAttempt))
                    return;
                recordingAttemptGeneration = graph.recordingAttemptGeneration();
                if(!transaction.bindRecordingAttemptWithinSubmissionOperation(graph, compiledGraph, recordingAttemptGeneration, &provisionalAttempt))
                    return;
                provisionalAttempt.complete();
            }
            transaction.rejectTaskWithinSubmissionOperation(graph, declarationAccess, compiledGraph, planAccess, task, recordingAttemptGeneration);
        }
    };
    GpuSubmissionPacketId packet;
    {
        GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::TryAcquire(graph);
        if(!declarationAccess.valid())
            return false;
        packet = planAccess.packetForTask(task);
        exceptionScope.setFailedPacket(packet);
        if(!planAccess.validFor(declarationAccess) || !transaction.validFor(planAccess) || !declarationAccess.validTask(task) || !planAccess.findTask(task).valid() || !planAccess.taskJoinsAcceptedQueueFrontier(task)){
            rejectTask(declarationAccess);
            return false;
        }
        if(!prepareRecordingAttemptAndBindTransactionWithinSubmissionOperation(compiledGraph, packet, artifactOperation, declarationAccess, planAccess, graph, recorder.m_timingRecorder, recordedGraph, transaction))
            return false;
    }
    if(!transaction.waitForSubmissionPublicationAndHasAcceptedPacketsWithinSubmissionOperation()){
        if(outFailedPacket)
            *outFailedPacket = packet;
        GpuTaskGraph::DeclarationReadView rejectionDeclarations = GpuTaskGraph::DeclarationReadView::TryAcquire(graph);
        rejectTask(rejectionDeclarations);
        return false;
    }

    return recordAndSubmitTaskWithinSubmissionOperation(compiledGraph, planAccess, artifactOperation, recorder, task, nullptr, nullptr, scratchArena, graph, recordedGraph, transaction, outFailedPacket);
}


bool GpuTaskScheduler::recordAndSubmitTask(
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecorder& recorder,
    const GpuTaskId task,
    const GpuTaskGraphTaskRecordedCallback* const recordedCallback,
    const GpuTaskGraphTaskAcceptedCallback* const acceptedCallback,
    Alloc::ScratchArena& scratchArena,
    GpuTaskGraph& graph,
    GpuRecordedGraph& recordedGraph,
    GpuGraphSubmissionTransaction& transaction,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    DeviceOperation deviceOperation(*this);
    if(!deviceOperation.m_admitted || &recorder.m_device != &device())
        return false;

    SubmissionAttemptExceptionFinalizer exceptionFinalizer(graph, transaction, compiledGraph, recordedGraph);
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;
    GpuRecordedGraph::ArtifactOperation artifactOperation(recordedGraph, GpuRecordedGraph::ArtifactOperationMode::Exclusive);
    if(!artifactOperation.valid())
        return false;
    GpuGraphSubmissionTransaction::SubmissionOperation submissionOperation(transaction, GpuGraphSubmissionTransaction::SubmissionOperationMode::CompositeBarrier, &artifactOperation);
    if(!submissionOperation.valid())
        return false;
    SubmissionAttemptExceptionScope exceptionScope(compiledGraph, recordedGraph, graph, transaction, outFailedPacket);

    {
        GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::TryAcquire(graph);
        if(!declarationAccess.valid() || !planAccess.validFor(declarationAccess) || !transaction.validFor(planAccess) || !declarationAccess.validTask(task) || !planAccess.findTask(task).valid())
            return false;

        const GpuSubmissionPacketId packet = planAccess.packetForTask(task);
        exceptionScope.setFailedPacket(packet);
        if(!prepareRecordingAttemptAndBindTransactionWithinSubmissionOperation(compiledGraph, packet, artifactOperation, declarationAccess, planAccess, graph, recorder.m_timingRecorder, recordedGraph, transaction))
            return false;
    }

    return recordAndSubmitTaskWithinSubmissionOperation(compiledGraph, planAccess, artifactOperation, recorder, task, recordedCallback, acceptedCallback, scratchArena, graph, recordedGraph, transaction, outFailedPacket);
}


bool GpuTaskScheduler::recordAndSubmitTaskWithinSubmissionOperation(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuRecordedGraph::ArtifactOperation& artifactAccess,
    const GpuNativePacketRecorder& recorder,
    const GpuTaskId task,
    const GpuTaskGraphTaskRecordedCallback* const recordedCallback,
    const GpuTaskGraphTaskAcceptedCallback* const acceptedCallback,
    Alloc::ScratchArena& scratchArena,
    GpuTaskGraph& graph,
    GpuRecordedGraph& recordedGraph,
    GpuGraphSubmissionTransaction& transaction,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(!planAccess.validFor(compiledGraph) || !artifactAccess.exclusiveFor(recordedGraph) || !GpuGraphSubmissionTransaction::SubmissionOperation::ActiveExclusiveFor(transaction))
        return false;

    GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::TryAcquire(graph);
    if(!declarationAccess.valid() || !planAccess.validFor(declarationAccess) || !transaction.validFor(planAccess) || !declarationAccess.validTask(task) || !planAccess.findTask(task).valid())
        return false;

    const u64 recordingAttemptGeneration = recordedGraph.recordingAttemptGeneration();
    if(recordingAttemptGeneration == 0u || !graph.matchesRecordingAttempt(compiledGraph, recordingAttemptGeneration))
        return false;

    const auto rejectTask = [&]{
        transaction.rejectTaskWithinSubmissionOperation(graph, declarationAccess, compiledGraph, planAccess, task, recordingAttemptGeneration);
    };
    if((recordedCallback && (!recordedCallback->invoke || recordedCallback->task != task)) || (acceptedCallback && (!acceptedCallback->invoke || acceptedCallback->task != task))){
        rejectTask();
        return false;
    }

    GpuSubmissionPacketId failedPacket;
    if(!recorder.recordTaskRangeInCompileOrder(graph, compiledGraph, task, task, scratchArena, recordedGraph, &failedPacket, nullptr)){
        if(outFailedPacket)
            *outFailedPacket = failedPacket;
        rejectTask();
        return false;
    }

    if(recordedCallback && !recordedCallback->invoke(recordedCallback->context, recordedGraph.packetStateSeed(planAccess.packetForTask(task), artifactAccess))){
        if(outFailedPacket)
            *outFailedPacket = planAccess.packetForTask(task);
        rejectTask();
        return false;
    }

    if(!submitPacketRangeWithinSubmissionOperation(compiledGraph, recordedGraph, planAccess.packetRangeForTasks(task, task), nullptr, 0u, acceptedCallback, acceptedCallback ? 1u : 0u, nullptr, 0u, scratchArena, graph, transaction, &failedPacket)){
        if(outFailedPacket)
            *outFailedPacket = failedPacket;
        // Range submission normally rejected the packet already. Keep this idempotent closeout for validation failures before traversal, so a renderer cannot strand an armed task.
        rejectTask();
        return false;
    }
    return true;
}


bool GpuTaskScheduler::prepareRecordingAttemptAndBindTransactionWithinSubmissionOperation(
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    const GpuRecordedGraph::ArtifactOperation& artifactAccess,
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph::ReadView& planAccess,
    GpuTaskGraph& graph,
    GpuTimingRecorder* const timingRecorder,
    GpuRecordedGraph& recordedGraph,
    GpuGraphSubmissionTransaction& transaction
)const{
    if(
        !planAccess.validFor(compiledGraph)
        || !artifactAccess.exclusiveFor(recordedGraph)
        || !GpuGraphSubmissionTransaction::SubmissionOperation::ActiveExclusiveFor(transaction)
        || !declarationAccess.validFor(graph)
        || !planAccess.validFor(declarationAccess)
        || !planAccess.validPacket(packet)
        || !transaction.validFor(planAccess)
    )
        return false;
    if(recordedGraph.validForWithinArtifactOperation(graph, declarationAccess, compiledGraph, planAccess, artifactAccess)){
        GpuTimingRecorder* const artifactTimingRecorder = recordedGraph.timingRecorderWithinArtifactOperation(artifactAccess);
        if(artifactTimingRecorder && artifactTimingRecorder != timingRecorder)
            return false;
        return transaction.bindRecordingAttemptWithinSubmissionOperation(graph, compiledGraph, recordedGraph.recordingAttemptGenerationWithinArtifactOperation(artifactAccess));
    }
    if(!recordedGraph.prepareRecordingStorageCandidate(compiledGraph, planAccess, timingRecorder))
        return false;

    GpuRecordingAttemptScope provisionalAttempt;
    if(!graph.beginRecordingAttempt(compiledGraph, packet, declarationAccess, planAccess, provisionalAttempt))
        return false;
    if(!provisionalAttempt.m_graph)
        return false;

    if(!transaction.bindRecordingAttemptWithinSubmissionOperation(graph, compiledGraph, provisionalAttempt.m_recordingAttemptGeneration, &provisionalAttempt))
        return false;
    recordedGraph.publishStorageCandidate(&graph, compiledGraph, planAccess, provisionalAttempt.m_recordingAttemptGeneration, artifactAccess);
    provisionalAttempt.complete();
    return recordedGraph.validForWithinArtifactOperation(graph, declarationAccess, compiledGraph, planAccess, artifactAccess);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

