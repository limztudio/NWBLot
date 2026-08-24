// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiler.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/command.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraph::applyCompiledBarrier(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledBarrier& barrier,
    CommandList& commandList
)const{
    if(
        !compiledGraph.validFor(*this)
        || !validResource(barrier.resource)
        || barrier.type >= GpuCompiledBarrierType::kCount
    )
        return false;

    const GpuGraphResourceNode& resource = m_resources[barrier.resource.index];
    const auto resolveOwnershipQueues = [&]{
        const GpuPhysicalQueueInfo* const sourceQueue = compiledGraph.queueInfo(barrier.sourceQueue);
        const GpuPhysicalQueueInfo* const destinationQueue = compiledGraph.queueInfo(barrier.destinationQueue);
        return sourceQueue
            && destinationQueue
            && sourceQueue->queueClass < CommandQueue::kCount
            && destinationQueue->queueClass < CommandQueue::kCount
        ;
    };
    switch(barrier.type){
    case GpuCompiledBarrierType::TextureTransition:
    case GpuCompiledBarrierType::TextureUav:{
        if(resource.type != GpuGraphResourceType::Texture || !resource.texture)
            return false;
        const ResourceStates::Mask permanentState = commandList.getPermanentTextureState(resource.texture.get());
        if(permanentState != ResourceStates::Unknown && permanentState != barrier.after)
            return false;
        if(barrier.isGraphInitialState){
            if(barrier.before == ResourceStates::Unknown)
                return false;
            const TextureSubresourceSet subresources = barrier.range.textureSubresources.resolve(
                resource.texture->getDescription(),
                TextureSubresourceMipResolve::Range
            );
            const MipLevel mipEnd = subresources.baseMipLevel + subresources.numMipLevels;
            const ArraySlice arrayEnd = subresources.baseArraySlice + subresources.numArraySlices;
            for(ArraySlice arraySlice = subresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
                for(MipLevel mipLevel = subresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
                    // CommandList::open can seed this exact subresource from an external handoff. Preserve that
                    // producer truth, but do not mistake a keep-initial-state descriptor fallback for a packet
                    // handoff: the graph declaration is authoritative when no explicit tracker state exists.
                    if(commandList.hasExplicitTextureSubresourceState(resource.texture.get(), arraySlice, mipLevel))
                        continue;
                    commandList.beginTrackingTextureState(
                        resource.texture.get(),
                        TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u),
                        barrier.before
                    );
                }
            }
        }
        commandList.setTextureState(
            resource.texture.get(),
            barrier.range.textureSubresources,
            barrier.after
        );
        return true;
    }
    case GpuCompiledBarrierType::BufferTransition:
    case GpuCompiledBarrierType::BufferUav:{
        if(resource.type != GpuGraphResourceType::Buffer || !resource.buffer)
            return false;
        const ResourceStates::Mask permanentState = commandList.getPermanentBufferState(resource.buffer.get());
        if(permanentState != ResourceStates::Unknown && permanentState != barrier.after)
            return false;
        if(barrier.isGraphInitialState){
            if(barrier.before == ResourceStates::Unknown)
                return false;
            if(!commandList.hasExplicitBufferState(resource.buffer.get()))
                commandList.beginTrackingBufferState(resource.buffer.get(), barrier.before);
        }
        commandList.setBufferState(resource.buffer.get(), barrier.after);
        return true;
    }
    case GpuCompiledBarrierType::TextureStateExport:{
        if(resource.type != GpuGraphResourceType::Texture || !resource.texture)
            return false;
        const ResourceStates::Mask permanentState = commandList.getPermanentTextureState(resource.texture.get());
        if(permanentState != ResourceStates::Unknown && permanentState != barrier.after)
            return false;
        // A task thunk may transition internally after its declared entry use. Reapply the compiler-required
        // final state against the native tracker, then retain it even when no Vulkan transition was needed so
        // packet close publishes a complete external handoff.
        commandList.setTextureState(
            resource.texture.get(),
            barrier.range.textureSubresources,
            barrier.after
        );
        commandList.beginTrackingTextureState(
            resource.texture.get(),
            barrier.range.textureSubresources,
            barrier.after
        );
        return true;
    }
    case GpuCompiledBarrierType::BufferStateExport:{
        if(resource.type != GpuGraphResourceType::Buffer || !resource.buffer)
            return false;
        const ResourceStates::Mask permanentState = commandList.getPermanentBufferState(resource.buffer.get());
        if(permanentState != ResourceStates::Unknown && permanentState != barrier.after)
            return false;
        commandList.setBufferState(resource.buffer.get(), barrier.after);
        commandList.beginTrackingBufferState(resource.buffer.get(), barrier.after);
        return true;
    }
    case GpuCompiledBarrierType::AccelStructStateExport:{
        if(resource.type != GpuGraphResourceType::AccelStruct || !resource.accelStruct)
            return false;
        Buffer* const backingBuffer = resource.accelStruct->getBackingBuffer();
        if(!backingBuffer)
            return false;
        const ResourceStates::Mask permanentState = commandList.getPermanentBufferState(backingBuffer);
        if(permanentState != ResourceStates::Unknown && permanentState != barrier.after)
            return false;
        // Acceleration-structure state and ownership are represented by Vulkan through the allocation that backs
        // the AS. Keep that lowering private to the graph runtime while retaining one typed graph resource.
        commandList.setAccelStructState(resource.accelStruct.get(), barrier.after);
        commandList.beginTrackingBufferState(backingBuffer, barrier.after);
        return true;
    }
    case GpuCompiledBarrierType::AccelStructTransition:
    case GpuCompiledBarrierType::AccelStructUav:{
        if(resource.type != GpuGraphResourceType::AccelStruct || !resource.accelStruct)
            return false;
        Buffer* const backingBuffer = resource.accelStruct->getBackingBuffer();
        if(!backingBuffer)
            return false;
        const ResourceStates::Mask permanentState = commandList.getPermanentBufferState(backingBuffer);
        if(permanentState != ResourceStates::Unknown && permanentState != barrier.after)
            return false;
        if(barrier.isGraphInitialState){
            if(barrier.before == ResourceStates::Unknown)
                return false;
            if(!commandList.hasExplicitBufferState(backingBuffer))
                commandList.beginTrackingBufferState(backingBuffer, barrier.before);
        }
        commandList.setAccelStructState(resource.accelStruct.get(), barrier.after);
        return true;
    }
    case GpuCompiledBarrierType::TextureOwnershipRelease:{
        const GpuPhysicalQueueInfo* const sourceQueue = compiledGraph.queueInfo(barrier.sourceQueue);
        const GpuPhysicalQueueInfo* const destinationQueue = compiledGraph.queueInfo(barrier.destinationQueue);
        if(
            resource.type != GpuGraphResourceType::Texture
            || !resource.texture
            || !resolveOwnershipQueues()
            || commandList.getDescription().physicalQueue != sourceQueue->id
        )
            return false;
        if(commandList.getPermanentTextureState(resource.texture.get()) != ResourceStates::Unknown)
            return false;
        commandList.releaseTextureOwnership(
            resource.texture.get(),
            barrier.range.textureSubresources,
            destinationQueue->id
        );
        return true;
    }
    case GpuCompiledBarrierType::BufferOwnershipRelease:{
        const GpuPhysicalQueueInfo* const sourceQueue = compiledGraph.queueInfo(barrier.sourceQueue);
        const GpuPhysicalQueueInfo* const destinationQueue = compiledGraph.queueInfo(barrier.destinationQueue);
        if(
            resource.type != GpuGraphResourceType::Buffer
            || !resource.buffer
            || !resolveOwnershipQueues()
            || commandList.getDescription().physicalQueue != sourceQueue->id
        )
            return false;
        if(commandList.getPermanentBufferState(resource.buffer.get()) != ResourceStates::Unknown)
            return false;
        commandList.releaseBufferOwnership(resource.buffer.get(), destinationQueue->id);
        return true;
    }
    case GpuCompiledBarrierType::AccelStructOwnershipRelease:{
        const GpuPhysicalQueueInfo* const sourceQueue = compiledGraph.queueInfo(barrier.sourceQueue);
        const GpuPhysicalQueueInfo* const destinationQueue = compiledGraph.queueInfo(barrier.destinationQueue);
        if(
            resource.type != GpuGraphResourceType::AccelStruct
            || !resource.accelStruct
            || !resolveOwnershipQueues()
            || commandList.getDescription().physicalQueue != sourceQueue->id
        )
            return false;
        Buffer* const backingBuffer = resource.accelStruct->getBackingBuffer();
        if(!backingBuffer)
            return false;
        if(commandList.getPermanentBufferState(backingBuffer) != ResourceStates::Unknown)
            return false;
        commandList.releaseBufferOwnership(backingBuffer, destinationQueue->id);
        return true;
    }
    case GpuCompiledBarrierType::TextureOwnershipAcquire:
    case GpuCompiledBarrierType::BufferOwnershipAcquire:
    case GpuCompiledBarrierType::AccelStructOwnershipAcquire:{
        const GpuPhysicalQueueInfo* const destinationQueue = compiledGraph.queueInfo(barrier.destinationQueue);
        if(!resolveOwnershipQueues() || commandList.getDescription().physicalQueue != destinationQueue->id)
            return false;

        // CommandList::open imports the compiler-selected producer state seed before packet prologue lowering. That
        // import emits the paired Vulkan acquire barrier with the exact exported layout, so this record is a checked
        // graph-plan marker rather than a second native acquire.
        return true;
    }
    default:
        return false;
    }
}

bool GpuTaskGraph::seedTaskRetainedResourceStates(
    const GpuTaskId& taskID,
    CommandList& commandList
)const{
    if(!validTask(taskID))
        return false;

    const GpuTaskNode& task = m_tasks[taskID.index];
    const GpuTaskResourceUse* const resourceUses = task.resourceUseCount != 0u
        ? m_resourceUses.data() + task.resourceUseOffset
        : nullptr
    ;
    if(task.resourceUseCount != 0u && !resourceUses)
        return false;
    for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
        const GpuTaskResourceUse& use = resourceUses[useIndex];
        if(!validResource(use.resource) || use.requiredState == ResourceStates::Unknown)
            return false;

        const GpuGraphResourceNode& resource = m_resources[use.resource.index];
        switch(resource.type){
        case GpuGraphResourceType::Texture:{
            if(!resource.texture)
                return false;
            const TextureDesc& description = resource.texture->getDescription();
            // Only seed a state that the Vulkan backend will retain exactly at packet close. Other graph resources
            // must already have an explicit compiler transition or native record-time state before they can become
            // a source.
            if(!description.keepInitialState || description.initialState != use.requiredState)
                continue;

            const TextureSubresourceSet subresources = use.range.textureSubresources.resolve(
                description,
                TextureSubresourceMipResolve::Range
            );
            if(subresources.numMipLevels == 0u || subresources.numArraySlices == 0u)
                return false;

            const MipLevel mipEnd = subresources.baseMipLevel + subresources.numMipLevels;
            const ArraySlice arrayEnd = subresources.baseArraySlice + subresources.numArraySlices;
            bool stateMatches = true;
            for(ArraySlice arraySlice = subresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
                for(MipLevel mipLevel = subresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
                    if(commandList.getTextureSubresourceState(resource.texture.get(), arraySlice, mipLevel) != use.requiredState){
                        stateMatches = false;
                        break;
                    }
                }
                if(!stateMatches)
                    break;
            }
            // A graph task can establish this state inside its own recording body (for example an upload changes
            // CopyDest back to a texture's retained ShaderResource state). Only materialize a state that is already
            // present before the task; the record body publishes its own state through the ordinary native tracker.
            if(!stateMatches)
                continue;
            commandList.beginTrackingTextureState(resource.texture.get(), subresources, use.requiredState);
            break;
        }
        case GpuGraphResourceType::Buffer:{
            bool alreadySeededByTask = false;
            for(usize previousUseIndex = 0u; previousUseIndex < useIndex; ++previousUseIndex){
                const GpuTaskResourceUse& previousUse = resourceUses[previousUseIndex];
                if(previousUse.resource == use.resource){
                    alreadySeededByTask = true;
                    break;
                }
            }
            if(alreadySeededByTask)
                continue;

            if(!resource.buffer)
                return false;
            const BufferDesc& description = resource.buffer->getDescription();
            if(!description.keepInitialState || description.initialState != use.requiredState)
                continue;
            if(commandList.getBufferState(resource.buffer.get()) != use.requiredState)
                return false;
            commandList.beginTrackingBufferState(resource.buffer.get(), use.requiredState);
            break;
        }
        case GpuGraphResourceType::AccelStruct:{
            bool alreadySeededByTask = false;
            for(usize previousUseIndex = 0u; previousUseIndex < useIndex; ++previousUseIndex){
                const GpuTaskResourceUse& previousUse = resourceUses[previousUseIndex];
                if(previousUse.resource == use.resource){
                    alreadySeededByTask = true;
                    break;
                }
            }
            if(alreadySeededByTask)
                continue;

            RayTracingAccelStruct* const accelStruct = resource.accelStruct.get();
            Buffer* const backingBuffer = accelStruct ? accelStruct->getBackingBuffer() : nullptr;
            if(!backingBuffer)
                return false;
            const BufferDesc& description = backingBuffer->getDescription();
            if(!description.keepInitialState || description.initialState != use.requiredState)
                continue;
            if(commandList.getBufferState(backingBuffer) != use.requiredState)
                return false;
            commandList.beginTrackingBufferState(backingBuffer, use.requiredState);
            break;
        }
        default:
            break;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

