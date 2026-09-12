// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"

#include "task_graph.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    if(outFailedPacket)
        *outFailedPacket = {};
    DeviceOperation deviceOperation(*this);
    if(!deviceOperation.m_admitted || &recorder.m_device != &device())
        return false;

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    if(outFailedPacket)
        *outFailedPacket = {};
    DeviceOperation deviceOperation(*this);
    if(!deviceOperation.m_admitted || &recorder.m_device != &device())
        return false;

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

