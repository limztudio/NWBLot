// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"
#include "compiler_internal.h"
#include "packet_runtime_internal.h"

#include <core/graphics/backend_selection/backend.h>

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
    Alloc::ScratchArena& scratchArena,
    GpuTaskGraphAnalysis& analysis,
    GpuTaskGraphQueueAssignments& assignments,
    GpuCompiledGraph& compiledGraph,
    GpuRecordedGraph& recordedGraph,
    GpuGraphSubmissionTransaction& transaction
)const{
    const GpuTaskGraphCompileOptions compileOptions;
    return scheduleGraph(graph, compileOptions, scratchArena, analysis, assignments, compiledGraph, recordedGraph, transaction);
}

bool GpuTaskScheduler::scheduleGraph(
    const GpuTaskGraph& graph,
    const GpuTaskGraphCompileOptions& compileOptions,
    Alloc::ScratchArena& scratchArena,
    GpuTaskGraphAnalysis& analysis,
    GpuTaskGraphQueueAssignments& assignments,
    GpuCompiledGraph& compiledGraph,
    GpuRecordedGraph& recordedGraph,
    GpuGraphSubmissionTransaction& transaction
)const{
    DeviceOperation deviceOperation(*this);
    if(!deviceOperation.m_admitted)
        return false;

    return compileGraph(graph, compileOptions, scratchArena, analysis, assignments, compiledGraph, recordedGraph, transaction);
}

bool GpuTaskScheduler::executeGraph(
    const GpuTaskGraphNormalExecutionDesc& desc,
    Alloc::ScratchArena& scratchArena,
    GpuTaskGraph& graph,
    GpuCompiledGraph& compiledGraph,
    GpuRecordedGraph& recordedGraph,
    GpuGraphSubmissionTransaction& transaction,
    GpuTimingRecorder* const timingRecorder,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    DeviceOperation deviceOperation(*this);
    if(!deviceOperation.m_admitted)
        return false;
    const GpuNativePacketRecorder recorder(device(), timingRecorder);
    return submitGraph(compiledGraph, recorder, desc, scratchArena, graph, recordedGraph, transaction, outFailedPacket);
}

bool GpuTaskScheduler::executeAcceptedFrontierTask(
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskId task,
    Alloc::ScratchArena& scratchArena,
    GpuTaskGraph& graph,
    GpuRecordedGraph& recordedGraph,
    GpuGraphSubmissionTransaction& transaction,
    GpuTimingRecorder* const timingRecorder,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    DeviceOperation deviceOperation(*this);
    if(!deviceOperation.m_admitted)
        return false;
    const GpuNativePacketRecorder recorder(device(), timingRecorder);
    return recordAndSubmitAcceptedFrontierTask(compiledGraph, recorder, task, scratchArena, graph, recordedGraph, transaction, outFailedPacket);
}

bool GpuTaskScheduler::executeTask(
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskId task,
    const GpuTaskGraphTaskRecordedCallback* const recordedCallback,
    const GpuTaskGraphTaskAcceptedCallback* const acceptedCallback,
    Alloc::ScratchArena& scratchArena,
    GpuTaskGraph& graph,
    GpuRecordedGraph& recordedGraph,
    GpuGraphSubmissionTransaction& transaction,
    GpuTimingRecorder* const timingRecorder,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    DeviceOperation deviceOperation(*this);
    if(!deviceOperation.m_admitted)
        return false;
    const GpuNativePacketRecorder recorder(device(), timingRecorder);
    return recordAndSubmitTask(compiledGraph, recorder, task, recordedCallback, acceptedCallback, scratchArena, graph, recordedGraph, transaction, outFailedPacket);
}

bool GpuTaskScheduler::compileGraph(
    const GpuTaskGraph& graph,
    const GpuTaskGraphCompileOptions& compileOptions,
    Alloc::ScratchArena& scratchArena,
    GpuTaskGraphAnalysis& analysis,
    GpuTaskGraphQueueAssignments& assignments,
    GpuCompiledGraph& compiledGraph,
    GpuRecordedGraph& recordedGraph,
    GpuGraphSubmissionTransaction& transaction
)const{
    const GpuPhysicalQueueTopology topology = device().getPhysicalQueueTopology();
    if(!topology.queues || topology.queueCount == 0u)
        return false;

    GpuTaskGraphCompileOptions resolvedOptions = compileOptions;
    Vector<GpuTaskQueueLoad, Alloc::ScratchArena> queueLoads(scratchArena);
    if(resolvedOptions.queueAssignmentOptions.queueLoadCount == 0u){
        queueLoads.reserve(topology.queueCount);
        for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
            const auto timelineResult = device().getQueueTimelineSnapshot(topology.queues[queueIndex].id);
            if(!timelineResult)
                return false;
            const GpuQueueTimelineSnapshot& timeline = *timelineResult;
            if(!timeline.valid())
                return false;
            const u64 pendingSubmissionCount = timeline.pendingSubmissionCount();
            const u64 estimatedCost = pendingSubmissionCount > Limit<u64>::s_Max / __hidden_gpu_task_scheduler_compilation::s_PendingSubmissionCost ? Limit<u64>::s_Max : pendingSubmissionCount * __hidden_gpu_task_scheduler_compilation::s_PendingSubmissionCost;
            queueLoads.push_back(GpuTaskQueueLoad{ .queue = timeline.queue, .estimatedCost = estimatedCost, });
        }
        resolvedOptions.queueAssignmentOptions.queueLoads = queueLoads.data();
        resolvedOptions.queueAssignmentOptions.queueLoadCount = queueLoads.size();
    }

    const GpuTaskGraph::DeclarationReadView declarations(graph);
    const GpuTaskGraphCompiler compiler;
    if(!compiler.compile(analysis, assignments, compiledGraph, scratchArena, declarations, topology, resolvedOptions))
        return false;

    recordedGraph.reset(compiledGraph);
    transaction.reset(compiledGraph);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

