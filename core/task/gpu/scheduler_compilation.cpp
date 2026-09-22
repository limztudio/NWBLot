// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"
#include "compiler_internal.h"
#include "packet_runtime_internal.h"

#include <core/graphics/backend_selection.h>

#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_scheduler_compilation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u64 s_PendingSubmissionCost = 4u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskScheduler::scheduleGraph(
    const GpuTaskGraph& graph,
    GpuTaskGraphAnalysis& analysis,
    GpuTaskGraphQueueAssignments& assignments,
    GpuCompiledGraph& compiledGraph,
    GpuRecordedGraph& recordedGraph,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena
)const{
    const GpuTaskGraphCompileOptions compileOptions;
    return scheduleGraph(
        graph,
        analysis,
        assignments,
        compiledGraph,
        recordedGraph,
        transaction,
        scratchArena,
        compileOptions
    );
}

bool GpuTaskScheduler::scheduleGraph(
    const GpuTaskGraph& graph,
    GpuTaskGraphAnalysis& analysis,
    GpuTaskGraphQueueAssignments& assignments,
    GpuCompiledGraph& compiledGraph,
    GpuRecordedGraph& recordedGraph,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    const GpuTaskGraphCompileOptions& compileOptions
)const{
    DeviceOperation deviceOperation(*this);
    if(!deviceOperation.m_admitted)
        return false;

    return compileGraph(
        graph,
        analysis,
        assignments,
        compiledGraph,
        recordedGraph,
        transaction,
        scratchArena,
        compileOptions
    );
}

bool GpuTaskScheduler::executeGraph(
    GpuTaskGraph& graph,
    GpuCompiledGraph& compiledGraph,
    GpuRecordedGraph& recordedGraph,
    const GpuTaskGraphNormalExecutionDesc& desc,
    GpuGraphSubmissionTransaction& transaction,
    GpuTimingRecorder* const timingRecorder,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    DeviceOperation deviceOperation(*this);
    if(!deviceOperation.m_admitted)
        return false;
    if(timingRecorder){
        const GpuNativePacketRecorder recorder(device(), *timingRecorder);
        return submitGraph(
            graph,
            compiledGraph,
            recorder,
            recordedGraph,
            desc,
            transaction,
            scratchArena,
            outFailedPacket
        );
    }
    const GpuNativePacketRecorder recorder(device());
    return submitGraph(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        desc,
        transaction,
        scratchArena,
        outFailedPacket
    );
}

bool GpuTaskScheduler::executeAcceptedFrontierTask(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    GpuRecordedGraph& recordedGraph,
    const GpuTaskId task,
    GpuGraphSubmissionTransaction& transaction,
    GpuTimingRecorder* const timingRecorder,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    DeviceOperation deviceOperation(*this);
    if(!deviceOperation.m_admitted)
        return false;
    if(timingRecorder){
        const GpuNativePacketRecorder recorder(device(), *timingRecorder);
        return recordAndSubmitAcceptedFrontierTask(
            graph,
            compiledGraph,
            recorder,
            recordedGraph,
            task,
            transaction,
            scratchArena,
            outFailedPacket
        );
    }
    const GpuNativePacketRecorder recorder(device());
    return recordAndSubmitAcceptedFrontierTask(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        task,
        transaction,
        scratchArena,
        outFailedPacket
    );
}

bool GpuTaskScheduler::executeTask(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    GpuRecordedGraph& recordedGraph,
    const GpuTaskId task,
    const GpuTaskGraphTaskRecordedCallback* const recordedCallback,
    GpuGraphSubmissionTransaction& transaction,
    GpuTimingRecorder* const timingRecorder,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket,
    const GpuTaskGraphTaskAcceptedCallback* const acceptedCallback
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    DeviceOperation deviceOperation(*this);
    if(!deviceOperation.m_admitted)
        return false;
    if(timingRecorder){
        const GpuNativePacketRecorder recorder(device(), *timingRecorder);
        return recordAndSubmitTask(
            graph,
            compiledGraph,
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
    const GpuNativePacketRecorder recorder(device());
    return recordAndSubmitTask(
        graph,
        compiledGraph,
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

bool GpuTaskScheduler::compileGraph(
    const GpuTaskGraph& graph,
    GpuTaskGraphAnalysis& analysis,
    GpuTaskGraphQueueAssignments& assignments,
    GpuCompiledGraph& compiledGraph,
    GpuRecordedGraph& recordedGraph,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    const GpuTaskGraphCompileOptions& compileOptions
)const{
    const GpuTaskGraphQueueTopology topology = device().getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount == 0u)
        return false;

    GpuTaskGraphCompileOptions resolvedOptions = compileOptions;
    Vector<GpuTaskQueueLoad, Alloc::ScratchArena> queueLoads(scratchArena);
    if(resolvedOptions.queueAssignmentOptions.queueLoadCount == 0u){
        queueLoads.reserve(topology.queueCount);
        for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
            const GpuQueueTimelineSnapshot timeline = device().getQueueTimelineSnapshot(topology.queues[queueIndex].id);
            if(!timeline.valid())
                return false;
            const u64 pendingSubmissionCount = timeline.pendingSubmissionCount();
            const u64 estimatedCost = pendingSubmissionCount > Limit<u64>::s_Max / __hidden_gpu_task_scheduler_compilation::s_PendingSubmissionCost
                ? Limit<u64>::s_Max
                : pendingSubmissionCount * __hidden_gpu_task_scheduler_compilation::s_PendingSubmissionCost
            ;
            queueLoads.push_back(GpuTaskQueueLoad{
                .queue = timeline.queue,
                .estimatedCost = estimatedCost,
            });
        }
        resolvedOptions.queueAssignmentOptions.queueLoads = queueLoads.data();
        resolvedOptions.queueAssignmentOptions.queueLoadCount = queueLoads.size();
    }

    const GpuTaskGraph::DeclarationReadView declarations(graph);
    const GpuTaskGraphCompiler compiler;
    if(!compiler.compile(declarations, analysis, topology, assignments, compiledGraph, scratchArena, resolvedOptions))
        return false;

    recordedGraph.reset(compiledGraph);
    transaction.reset(compiledGraph);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

