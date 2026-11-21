// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"
#include "compiler_internal.h"
#include "packet_runtime_internal.h"
#include "scheduler_callback_bindings.h"

#include <core/graphics/backend_selection/backend.h>

#include <global/limit.h>
#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_scheduler_compilation{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u64 s_PendingSubmissionCost = 4u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuTaskScheduler::scheduleGraph(
    const GpuTaskGraph& graph, const GpuTaskGraphCompileOptions& options, GpuTaskGraphAnalysis& analysis,
    GpuTaskGraphQueueAssignments& assignments, GpuCompiledGraph& compiled, GpuRecordedGraph& recorded, GpuGraphSubmissionTransaction& transaction
)const{
    Alloc::ScratchArena scratch;
    return scheduleGraph(graph, options, scratch, analysis, assignments, compiled, recorded, transaction);
}

b8 GpuTaskScheduler::executeGraphWithTiming(
    const GpuTaskGraphNormalExecutionDesc& desc,
    GpuTaskGraph& graph,
    GpuCompiledGraph& compiled,
    GpuRecordedGraph& recorded,
    GpuGraphSubmissionTransaction& transaction,
    GpuTimingRecorder& timing
)const{
    Alloc::ScratchArena scratch;
    return executeGraph(desc, scratch, graph, compiled, recorded, transaction, &timing);
}

b8 GpuTaskScheduler::executeAcceptedFrontierTask(
    const GpuCompiledGraph& compiled,
    GpuTaskId task,
    GpuTaskGraph& graph,
    GpuRecordedGraph& recorded,
    GpuGraphSubmissionTransaction& transaction,
    GpuTimingRecorder& timing
)const{
    Alloc::ScratchArena scratch;
    return executeAcceptedFrontierTask(compiled, task, scratch, graph, recorded, transaction, &timing);
}

b8 GpuTaskScheduler::executeTask(
    const GpuCompiledGraph& compiled,
    GpuTaskId task,
    const GpuTaskGraphTaskRecordedCallback& onRecorded,
    const GpuTaskGraphTaskAcceptedCallback& onAccepted,
    GpuTaskGraph& graph,
    GpuRecordedGraph& recorded,
    GpuGraphSubmissionTransaction& transaction,
    GpuTimingRecorder& timing
)const{
    Alloc::ScratchArena scratch;
    return executeTask(compiled, task, &onRecorded, &onAccepted, scratch, graph, recorded, transaction, &timing);
}

b8 GpuTaskScheduler::executeGraph(
    const GpuTaskGraphNormalExecutionDesc& desc, GpuTaskGraph& graph,
    GpuCompiledGraph& compiled, GpuRecordedGraph& recorded, GpuGraphSubmissionTransaction& transaction
)const{
    Alloc::ScratchArena scratch;
    return executeGraph(desc, scratch, graph, compiled, recorded, transaction, nullptr);
}

b8 GpuTaskScheduler::scheduleGraph(
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

b8 GpuTaskScheduler::scheduleGraph(
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

b8 GpuTaskScheduler::executeGraph(
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
    return submitGraphWithinDeviceOperation(compiledGraph, recorder, desc, scratchArena, graph, recordedGraph, transaction, outFailedPacket);
}

b8 GpuTaskScheduler::executeAcceptedFrontierTask(
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

b8 GpuTaskScheduler::executeTask(
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

b8 GpuTaskScheduler::compileGraph(
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
            const u64 estimatedCost = MultiplySaturatingU64(pendingSubmissionCount, __hidden_gpu_task_scheduler_compilation::s_PendingSubmissionCost);
            queueLoads.push_back(GpuTaskQueueLoad{ .queue = timeline.queue, .estimatedCost = estimatedCost, });
        }
        resolvedOptions.queueAssignmentOptions.queueLoads = queueLoads.data();
        resolvedOptions.queueAssignmentOptions.queueLoadCount = queueLoads.size();
    }

    const GpuTaskGraph::DeclarationReadView declarations(graph);
    const GpuTaskGraphCompiler compiler;
    if(!compiler.compile(declarations, topology, resolvedOptions, scratchArena, analysis, assignments, compiledGraph))
        return false;

    recordedGraph.reset(compiledGraph);
    transaction.reset(compiledGraph);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskSubmissionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TaskCallback>
b8 TaskCallbackBindings<TaskCallback>::resolve(
    const GpuTaskGraph::DeclarationReadView& declarations,
    const GpuCompiledGraph::ReadView& plan,
    const GpuSubmissionPacketRange& range,
    const TaskCallback* const callbacks,
    const usize callbackCount
){
    m_callbacks = nullptr;
    m_callbackCount = 0u;
    m_generation = 0u;
    m_indices.clear();
    if((callbackCount != 0u && !callbacks) || !plan.validFor(declarations) || !plan.validPacketRange(range) || callbackCount > declarations.taskCount())
        return false;
    const b8 useIndexTable = callbackCount > s_InlineCallbackCount;
    if(useIndexTable)
        m_indices.assign(declarations.taskCount(), Limit<u32>::s_Max);
    const usize rangeEnd = static_cast<usize>(range.first.index) + range.packetCount;
    for(usize callbackIndex = 0u; callbackIndex < callbackCount; ++callbackIndex){
        const TaskCallback& callback = callbacks[callbackIndex];
        if(!callback.invoke || !declarations.validTask(callback.task))
            return false;
        const GpuCompiledTaskView task = plan.findTask(callback.task);
        if(!task.valid())
            return false;
        const GpuSubmissionPacketId packet = task.plan->packet;
        if(!packet.valid() || packet.index < range.first.index || static_cast<usize>(packet.index) >= rangeEnd)
            return false;
        if(useIndexTable){
            if(m_indices[callback.task.index] != Limit<u32>::s_Max)
                return false;
            m_indices[callback.task.index] = static_cast<u32>(callbackIndex);
        }
        else{
            for(usize previousIndex = 0u; previousIndex < callbackIndex; ++previousIndex){
                if(callbacks[previousIndex].task == callback.task)
                    return false;
            }
        }
    }
    m_callbacks = callbacks;
    m_callbackCount = callbackCount;
    m_generation = declarations.generation();
    return true;
}

template<typename TaskCallback>
const TaskCallback* TaskCallbackBindings<TaskCallback>::find(const GpuTaskId task)const noexcept{
    if(m_callbackCount == 0u || !m_callbacks || !task.valid() || task.generation != m_generation)
        return nullptr;
    if(!m_indices.empty()){
        if(task.index >= m_indices.size())
            return nullptr;
        const u32 index = m_indices[task.index];
        return index == Limit<u32>::s_Max ? nullptr : &m_callbacks[index];
    }
    for(usize callbackIndex = 0u; callbackIndex < m_callbackCount; ++callbackIndex){
        if(m_callbacks[callbackIndex].task == task)
            return &m_callbacks[callbackIndex];
    }
    return nullptr;
}

template class TaskCallbackBindings<GpuTaskGraphTaskRecordedCallback>;
template class TaskCallbackBindings<GpuTaskGraphTaskAcceptedCallback>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

