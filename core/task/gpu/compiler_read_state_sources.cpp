// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_read_state_sources.h"

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/backend_selection/resource_validation.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_compiler_read_state_sources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace GpuTaskGraphCompilerDetail;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ReadStatesCompatible(const GpuTaskGraphResourceView& resource, const ResourceStates::Mask lhs, const ResourceStates::Mask rhs)noexcept{
    return lhs == rhs || (resource.type == GpuGraphResourceType::Texture && GraphicsBackend::AreTextureReadStatesCompatible(lhs, rhs));
}

[[nodiscard]] bool NativeClosePreservesState(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphResourceView& resource,
    const ResourceStates::Mask state
)noexcept{
    if(const Texture* const texture = graph.textureForResource(resource.id)){
        const TextureDesc& description = texture->getCreationDescription();
        return !description.keepInitialState || description.initialState == state;
    }
    const Buffer* buffer = graph.bufferForResource(resource.id);
    if(const RayTracingAccelStruct* const accelStruct = graph.accelStructForResource(resource.id))
        buffer = accelStruct->getBackingBuffer();
    if(buffer){
        // Buffer restoration changes synchronization masks but has no image layout; the consumer reseeds its exact read state.
        const BufferDesc& description = buffer->getCreationDescription();
        return !description.keepInitialState || description.initialState != ResourceStates::Unknown;
    }
    return !resource.hasBackendResource;
}

[[nodiscard]] bool PacketPreservesReadStateSource(
    const GpuTaskGraphResourceStatePlan& plan,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    const TrackedCompiledResourceState& source
)noexcept{
    if(!NativeClosePreservesState(plan.graph, resource, source.state))
        return false;
    const GpuSubmissionPacketId packetID = FindCompiledPacketForTask(plan.compiledPlan, source.task);
    if(!packetID.valid() || packetID.generation != plan.compiledPlan.planGeneration || packetID.index >= plan.compiledPlan.packets.size())
        return false;
    const GpuSubmissionPacket& packet = plan.compiledPlan.packets[packetID.index];
    bool foundSource = false;
    for(u32 taskOffset = 0u; taskOffset < packet.taskCount; ++taskOffset){
        const GpuTaskId taskID = plan.compiledPlan.packetTasks[packet.taskOffset + taskOffset];
        foundSource = foundSource || taskID == source.task;
        if(!foundSource)
            continue;
        const GpuTaskGraphTaskView task = plan.graph.taskAt(taskID.index);
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            if(use.resource != resource.id || !RangesOverlap(resource, use.range, range))
                continue;
            if(use.access != GpuTaskResourceAccess::Read || use.requiredState != source.state)
                return false;
        }
    }
    if(!foundSource)
        return false;
    for(const PendingCompiledEpilogueBarrier& pending : plan.pendingEpilogueBarriers){
        if(pending.barrier.resource != resource.id || !RangesOverlap(resource, pending.barrier.range, range))
            continue;
        if(FindCompiledPacketForTask(plan.compiledPlan, pending.task) == packetID)
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool TaskPreservesReadState(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const TaskResourceUseIndex& useHistory,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    const usize useIndex
)noexcept{
    if(!useHistory.validFor(task) || useIndex >= task.resourceUseCount || task.resourceUses[useIndex].resource != resource.id)
        return false;
    const GpuTaskResourceUse& entryUse = task.resourceUses[useIndex];
    if(entryUse.access != GpuTaskResourceAccess::Read)
        return false;
    for(usize nextUseIndex = useHistory.next(useIndex); nextUseIndex != Limit<usize>::s_Max; nextUseIndex = useHistory.next(nextUseIndex)){
        const GpuTaskResourceUse& laterUse = task.resourceUses[nextUseIndex];
        GpuTaskResourceRange laterRange;
        if(!ResolveResourceRangeForPlanning(graph, resource, laterUse.range, laterRange))
            return false;
        if(!RangesOverlap(resource, laterRange, range))
            continue;
        if(laterUse.access != GpuTaskResourceAccess::Read
            || !__hidden_gpu_task_graph_compiler_read_state_sources::ReadStatesCompatible(resource, entryUse.requiredState, laterUse.requiredState))
            return false;
    }
    return true;
}

const TrackedCompiledResourceState* FindConcurrentReadStateSource(
    const GpuTaskGraphResourceStatePlan& plan,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    const TrackedCompiledResourceState& previousState,
    const GpuTaskResourceUse& use,
    const GpuPhysicalQueueInfo& destinationQueue
)noexcept{
    if(
        previousState.access != GpuTaskResourceAccess::Read
        || use.access != GpuTaskResourceAccess::Read
        || !__hidden_gpu_task_graph_compiler_read_state_sources::ReadStatesCompatible(resource, previousState.state, use.requiredState)
        || previousState.readStateSourceIndex >= plan.trackedResourceStates.size()
        || !__hidden_gpu_task_graph_compiler_read_state_sources::NativeClosePreservesState(plan.graph, resource, use.requiredState)
    )
        return nullptr;
    const TrackedCompiledResourceState& source = plan.trackedResourceStates[previousState.readStateSourceIndex];
    const GpuPhysicalQueueInfo* const previousQueue = FindCompiledQueueInfo(plan.compiledPlan, previousState.queue);
    const GpuPhysicalQueueInfo* const sourceQueue = FindCompiledQueueInfo(plan.compiledPlan, source.queue);
    if(
        source.resource != resource.id
        || source.access != GpuTaskResourceAccess::Read
        || !__hidden_gpu_task_graph_compiler_read_state_sources::ReadStatesCompatible(resource, source.state, use.requiredState)
        || !RangeContains(resource, source.range, range)
        || !previousQueue
        || !sourceQueue
        || !ResourceUsesConcurrentQueueSharing(resource, plan.topology)
        || !ResourceSharingAdmitsQueue(resource, plan.topology, *previousQueue)
        || !ResourceSharingAdmitsQueue(resource, plan.topology, *sourceQueue)
        || !ResourceSharingAdmitsQueue(resource, plan.topology, destinationQueue)
    )
        return nullptr;
    const usize previousStateIndex = static_cast<usize>(&previousState - plan.trackedResourceStates.data());
    if(previousState.readStateSourceIndex > previousStateIndex)
        return nullptr;
    // A propagated earlier index already passed the complete packet-close proof; only a new anchor scans its packet.
    if(previousState.readStateSourceIndex == previousStateIndex
        && !__hidden_gpu_task_graph_compiler_read_state_sources::PacketPreservesReadStateSource(plan, resource, source.range, source))
        return nullptr;
    return &source;
}

ResourceStates::Mask ReadStateSourceSnapshotState(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphResourceView& resource,
    const ResourceStates::Mask state
)noexcept{
    const Buffer* buffer = graph.bufferForResource(resource.id);
    if(const RayTracingAccelStruct* const accelStruct = graph.accelStructForResource(resource.id))
        buffer = accelStruct->getBackingBuffer();
    if(buffer){
        const BufferDesc& description = buffer->getCreationDescription();
        if(description.keepInitialState)
            return description.initialState;
    }
    return state;
}

bool AppendOverlappingReaderDependencies(
    GpuTaskGraphResourceStatePlan& plan,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    const TrackedCompiledResourceState& previousState,
    const GpuSubmissionPacketId& consumer
){
    // Exclusive sharing always chains overlapping readers through their latest state seeds.
    if(!ResourceUsesConcurrentQueueSharing(resource, plan.topology))
        return true;
    if(previousState.resource != resource.id || previousState.access != GpuTaskResourceAccess::Read
        || !RangeContains(resource, previousState.range, range))
        return false;
    GpuTaskId newerOverlappingTask;
    bool mayBoundReadEpoch = true;
    for(usize stateIndex = plan.resourceHistory.last(resource.id); stateIndex != Limit<usize>::s_Max; stateIndex = plan.resourceHistory.previous(stateIndex)){
        const TrackedCompiledResourceState& reader = plan.trackedResourceStates[stateIndex];
        if(!RangesOverlap(resource, reader.range, range))
            continue;
        if(IsWriteAccess(reader.access)){
            // A covering writer joins preceding readers through hazards; subsequent readers depend on that writer.
            if(RangeContains(resource, reader.range, range))
                return true;
            mayBoundReadEpoch = false;
            newerOverlappingTask = reader.task;
            continue;
        }
        if(reader.access != GpuTaskResourceAccess::Read){
            mayBoundReadEpoch = false;
            newerOverlappingTask = reader.task;
            continue;
        }
        if(!__hidden_gpu_task_graph_compiler_read_state_sources::ReadStatesCompatible(resource, reader.state, previousState.state)){
            // The newer covering read state already joined this older epoch. Partial or task-local changes need full history.
            if(mayBoundReadEpoch && newerOverlappingTask.valid() && reader.task != newerOverlappingTask
                && RangeContains(resource, reader.range, range))
                return true;
            mayBoundReadEpoch = false;
        }
        newerOverlappingTask = reader.task;
        const GpuSubmissionPacketId producer = FindCompiledPacketForTask(plan.compiledPlan, reader.task);
        if(!producer.valid() || producer.generation != consumer.generation || producer.index > consumer.index)
            return false;
        if(producer == consumer)
            continue;
        if(!plan.resourceStateDependencies.empty()){
            const GpuPacketDependency& last = plan.resourceStateDependencies.back();
            if(last.producer == producer && last.consumer == consumer)
                continue;
        }
        plan.resourceStateDependencies.push_back(GpuPacketDependency{ .producer = producer, .consumer = consumer });
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

