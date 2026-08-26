// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_imports{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CompatibleResourceMetadata(
    const GpuTaskGraphResourceView& resource,
    const CommandListResourceStateHandoff* const initialOwnerStateSourceIdentity,
    const GpuGraphResourceDesc& desc
)noexcept{
    // Re-importing a multi-producer source through the ordinary typed-import overload could silently exchange one
    // of its immutable ownership snapshots. Require callers to reuse its graph resource ID instead.
    if(
        resource.initialOwnerHandoffSourceCount != 0u
        || desc.initialOwnerHandoffSources
        || desc.initialOwnerHandoffSourceCount != 0u
    )
        return false;
    return resource.identity == desc.identity
        && resource.type == desc.type
        && resource.initialState == desc.initialState
        && resource.externalFinalState == desc.externalFinalState
        && resource.externalFinalReleaseDestinationQueue == desc.externalFinalReleaseDestinationQueue
        && resource.initialOwnerQueue == desc.initialOwnerQueue
        && resource.initialOwnerReleaseDestinationQueue == desc.initialOwnerReleaseDestinationQueue
        && resource.initialOwnerCompletion == desc.initialOwnerCompletion
        && resource.initialOwnerMinimumCompletionToken.queue == desc.initialOwnerMinimumCompletionToken.queue
        && resource.initialOwnerMinimumCompletionToken.value == desc.initialOwnerMinimumCompletionToken.value
        && resource.initialOwnerMinimumCompletionToken.physicalQueueIndex == desc.initialOwnerMinimumCompletionToken.physicalQueueIndex
        && resource.initialOwnerMinimumCompletionToken.deviceGeneration == desc.initialOwnerMinimumCompletionToken.deviceGeneration
        && initialOwnerStateSourceIdentity == desc.initialOwnerStateSource
        && resource.queueSharing == desc.queueSharing;
}

[[nodiscard]] static bool CompatibleRetainedExternalFinalState(
    const bool keepInitialState,
    const ResourceStates::Mask nativeInitialState,
    const ResourceStates::Mask externalFinalState
)noexcept{
    // Native command-list close restores retained resources to their descriptor state. A typed graph import must
    // not publish a different known terminal state after that restoration.
    return !keepInitialState || externalFinalState == ResourceStates::Unknown || externalFinalState == nativeInitialState;
}

[[nodiscard]] static bool CompatiblePipelineMetadata(
    const GpuTaskGraphPipelineView& pipeline,
    const GpuGraphPipelineDesc& desc
)noexcept{
    // Identity and concrete pipeline kind define the graph-side table key.  Marker text is observational metadata,
    // matching resource imports where a later compatible import reuses the original graph-owned label.
    return pipeline.identity == desc.identity && pipeline.type == desc.type;
}

[[nodiscard]] static bool HasExternalCompletionTokenValue(const QueueSubmissionToken& token)noexcept{
    return token.queue != CommandQueue::kCount
        || token.value != 0u
        || token.physicalQueueIndex != Limit<u16>::s_Max
        || token.deviceGeneration != 0u
    ;
}

[[nodiscard]] static bool SameSubmissionToken(
    const QueueSubmissionToken& lhs,
    const QueueSubmissionToken& rhs
)noexcept{
    return lhs.queue == rhs.queue
        && lhs.value == rhs.value
        && lhs.physicalQueueIndex == rhs.physicalQueueIndex
        && lhs.deviceGeneration == rhs.deviceGeneration
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuGraphResourceId GpuTaskGraph::importResource(const GpuGraphResourceDesc& desc){
    if(!desc.identity || desc.markerLabel.empty() || desc.type >= GpuGraphResourceType::kCount)
        return {};

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuTaskGraphResourceView existing = resourceAt(resourceIndex);
        if(existing.identity != desc.identity)
            continue;
        if(!__hidden_gpu_task_graph_imports::CompatibleResourceMetadata(
            existing,
            m_resources[resourceIndex].initialOwnerStateSourceIdentity,
            desc
        ))
            return {};
        return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
    }

    return appendResource(desc);
}

GpuGraphResourceId GpuTaskGraph::importTexture(const TextureHandle& texture, const GpuGraphResourceDesc& desc){
    if(!texture || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphResourceType::Texture)
        return {};
    if(!texture->descriptionMatchesCreation())
        return {};

    const TextureDesc& textureDesc = texture->getCreationDescription();
    if(!__hidden_gpu_task_graph_imports::CompatibleRetainedExternalFinalState(
        textureDesc.keepInitialState,
        textureDesc.initialState,
        desc.externalFinalState
    ))
        return {};

    GpuGraphResourceDesc resolvedDesc = desc;
    if(!resolvedDesc.hasExplicitInitialState && resolvedDesc.initialState == ResourceStates::Unknown){
        // A mixed retained texture has no single physical layout: an accepted partial upload restored only part of
        // it, while its remaining subresources are still Undefined. Preserve the legacy descriptor fallback for
        // all-unknown fresh textures and all-known native imports, but force this ambiguous typed import to name
        // Unknown until the caller supplies an explicit per-resource state source.
        resolvedDesc.initialState = textureDesc.keepInitialState && texture->hasPartiallyKnownRetainedSubresourceState()
            ? ResourceStates::Unknown
            : textureDesc.initialState
        ;
    }
    if(resolvedDesc.queueSharing != ResourceQueueSharing::Exclusive && resolvedDesc.queueSharing != textureDesc.queueSharing)
        return {};
    resolvedDesc.queueSharing = textureDesc.queueSharing;

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuGraphResourceNode& existing = m_resources[resourceIndex];
        if(existing.type == GpuGraphResourceType::Texture && existing.texture.get() == texture.get()){
            if(!__hidden_gpu_task_graph_imports::CompatibleResourceMetadata(
                resourceAt(resourceIndex),
                existing.initialOwnerStateSourceIdentity,
                resolvedDesc
            ))
                return {};
            return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
        }
        if(existing.identity == resolvedDesc.identity)
            return {};
    }

    const GpuGraphResourceId resource = appendResource(resolvedDesc);
    if(resource.valid()){
        GpuGraphResourceNode& importedResource = m_resources[resource.index];
        importedResource.texture = texture;
        importedResource.deviceGeneration = texture->getDeviceGeneration();
    }
    return resource;
}

GpuGraphResourceId GpuTaskGraph::importBuffer(const BufferHandle& buffer, const GpuGraphResourceDesc& desc){
    if(
        !buffer
        || !buffer->descriptionMatchesCreation()
        || !desc.identity
        || desc.markerLabel.empty()
        || desc.type != GpuGraphResourceType::Buffer
    )
        return {};

    const BufferDesc& bufferDesc = buffer->getCreationDescription();
    if(!__hidden_gpu_task_graph_imports::CompatibleRetainedExternalFinalState(
        bufferDesc.keepInitialState,
        bufferDesc.initialState,
        desc.externalFinalState
    ))
        return {};

    GpuGraphResourceDesc resolvedDesc = desc;
    if(!resolvedDesc.hasExplicitInitialState && resolvedDesc.initialState == ResourceStates::Unknown)
        resolvedDesc.initialState = bufferDesc.initialState;
    if(resolvedDesc.queueSharing != ResourceQueueSharing::Exclusive && resolvedDesc.queueSharing != bufferDesc.queueSharing)
        return {};
    resolvedDesc.queueSharing = bufferDesc.queueSharing;

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuGraphResourceNode& existing = m_resources[resourceIndex];
        if(existing.type == GpuGraphResourceType::Buffer && existing.buffer.get() == buffer.get()){
            if(!__hidden_gpu_task_graph_imports::CompatibleResourceMetadata(
                resourceAt(resourceIndex),
                existing.initialOwnerStateSourceIdentity,
                resolvedDesc
            ))
                return {};
            return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
        }
        if(existing.identity == resolvedDesc.identity)
            return {};
    }

    const GpuGraphResourceId resource = appendResource(resolvedDesc);
    if(resource.valid()){
        GpuGraphResourceNode& importedResource = m_resources[resource.index];
        importedResource.buffer = buffer;
        importedResource.deviceGeneration = buffer->getDeviceGeneration();
    }
    return resource;
}

GpuGraphResourceId GpuTaskGraph::findImportedTexture(const TextureHandle& texture)const noexcept{
    if(!texture)
        return {};

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuGraphResourceNode& resource = m_resources[resourceIndex];
        if(resource.type == GpuGraphResourceType::Texture && resource.texture.get() == texture.get())
            return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
    }
    return {};
}

GpuGraphResourceId GpuTaskGraph::findImportedBuffer(const BufferHandle& buffer)const noexcept{
    if(!buffer)
        return {};

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuGraphResourceNode& existing = m_resources[resourceIndex];
        if(existing.type == GpuGraphResourceType::Buffer && existing.buffer.get() == buffer.get())
            return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
    }
    return {};
}

GpuGraphResourceId GpuTaskGraph::importAccelStruct(
    const RayTracingAccelStructHandle& accelStruct,
    const GpuGraphResourceDesc& desc
){
    if(!accelStruct || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphResourceType::AccelStruct)
        return {};

    const ResourceQueueSharing::Mask creationQueueSharing = accelStruct->getCreationQueueSharing();
    if(!accelStruct->queueSharingMatchesCreation())
        return {};
    if(const Buffer* const backingBuffer = accelStruct->getBackingBuffer()){
        const BufferDesc& backingBufferDesc = backingBuffer->getCreationDescription();
        if(
            !backingBuffer->descriptionMatchesCreation()
            || backingBufferDesc.queueSharing != creationQueueSharing
            || !__hidden_gpu_task_graph_imports::CompatibleRetainedExternalFinalState(
                backingBufferDesc.keepInitialState,
                backingBufferDesc.initialState,
                desc.externalFinalState
            )
        )
            return {};
    }

    GpuGraphResourceDesc resolvedDesc = desc;
    if(
        resolvedDesc.queueSharing != ResourceQueueSharing::Exclusive
        && resolvedDesc.queueSharing != creationQueueSharing
    )
        return {};
    resolvedDesc.queueSharing = creationQueueSharing;

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuGraphResourceNode& existing = m_resources[resourceIndex];
        if(existing.type == GpuGraphResourceType::AccelStruct && existing.accelStruct.get() == accelStruct.get()){
            if(!__hidden_gpu_task_graph_imports::CompatibleResourceMetadata(
                resourceAt(resourceIndex),
                existing.initialOwnerStateSourceIdentity,
                resolvedDesc
            ))
                return {};
            return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
        }
        if(existing.identity == resolvedDesc.identity)
            return {};
    }

    const GpuGraphResourceId resource = appendResource(resolvedDesc);
    if(resource.valid()){
        GpuGraphResourceNode& importedResource = m_resources[resource.index];
        importedResource.accelStruct = accelStruct;
        importedResource.deviceGeneration = accelStruct->getDeviceGeneration();
    }
    return resource;
}

GpuGraphResourceId GpuTaskGraph::importHazardDomain(const GpuGraphResourceDesc& desc){
    if(desc.type != GpuGraphResourceType::HazardDomain)
        return {};
    return importResource(desc);
}

GpuGraphResourceSetId GpuTaskGraph::importResourceSet(const GpuGraphResourceSetDesc& desc){
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || (desc.memberCount != 0u && !desc.members)
    )
        return {};

    for(usize resourceSetIndex = 0u; resourceSetIndex < m_resourceSets.size(); ++resourceSetIndex){
        const GpuTaskGraphResourceSetView existing = resourceSetAt(resourceSetIndex);
        if(existing.identity != desc.identity)
            continue;
        if(existing.memberCount != desc.memberCount)
            return {};
        for(usize memberIndex = 0u; memberIndex < desc.memberCount; ++memberIndex){
            if(existing.members[memberIndex] != desc.members[memberIndex])
                return {};
        }
        return existing.id;
    }

    return appendResourceSet(desc);
}

GpuGraphPipelineId GpuTaskGraph::importPipeline(const GpuGraphPipelineDesc& desc){
    if(!desc.identity || desc.markerLabel.empty() || desc.type >= GpuGraphPipelineType::kCount)
        return {};

    for(usize pipelineIndex = 0u; pipelineIndex < m_pipelines.size(); ++pipelineIndex){
        const GpuTaskGraphPipelineView existing = pipelineAt(pipelineIndex);
        if(existing.identity != desc.identity)
            continue;
        if(!__hidden_gpu_task_graph_imports::CompatiblePipelineMetadata(existing, desc))
            return {};
        return GpuGraphPipelineId{ static_cast<u32>(pipelineIndex), m_generation };
    }

    return appendPipeline(desc);
}

GpuGraphPipelineId GpuTaskGraph::importGraphicsPipeline(
    const GraphicsPipelineHandle& pipeline,
    const GpuGraphPipelineDesc& desc
){
    if(!pipeline || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphPipelineType::Graphics)
        return {};

    for(usize pipelineIndex = 0u; pipelineIndex < m_pipelines.size(); ++pipelineIndex){
        const GpuGraphPipelineNode& existing = m_pipelines[pipelineIndex];
        if(existing.type == GpuGraphPipelineType::Graphics && existing.graphicsPipeline.get() == pipeline.get()){
            if(!__hidden_gpu_task_graph_imports::CompatiblePipelineMetadata(pipelineAt(pipelineIndex), desc))
                return {};
            return GpuGraphPipelineId{ static_cast<u32>(pipelineIndex), m_generation };
        }
        if(existing.identity == desc.identity)
            return {};
    }

    const GpuGraphPipelineId id = appendPipeline(desc);
    if(id.valid()){
        GpuGraphPipelineNode& importedPipeline = m_pipelines[id.index];
        importedPipeline.graphicsPipeline = pipeline;
        importedPipeline.deviceGeneration = pipeline->getDeviceGeneration();
    }
    return id;
}

GpuGraphPipelineId GpuTaskGraph::importComputePipeline(
    const ComputePipelineHandle& pipeline,
    const GpuGraphPipelineDesc& desc
){
    if(!pipeline || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphPipelineType::Compute)
        return {};

    for(usize pipelineIndex = 0u; pipelineIndex < m_pipelines.size(); ++pipelineIndex){
        const GpuGraphPipelineNode& existing = m_pipelines[pipelineIndex];
        if(existing.type == GpuGraphPipelineType::Compute && existing.computePipeline.get() == pipeline.get()){
            if(!__hidden_gpu_task_graph_imports::CompatiblePipelineMetadata(pipelineAt(pipelineIndex), desc))
                return {};
            return GpuGraphPipelineId{ static_cast<u32>(pipelineIndex), m_generation };
        }
        if(existing.identity == desc.identity)
            return {};
    }

    const GpuGraphPipelineId id = appendPipeline(desc);
    if(id.valid()){
        GpuGraphPipelineNode& importedPipeline = m_pipelines[id.index];
        importedPipeline.computePipeline = pipeline;
        importedPipeline.deviceGeneration = pipeline->getDeviceGeneration();
    }
    return id;
}

GpuGraphPipelineId GpuTaskGraph::importMeshletPipeline(
    const MeshletPipelineHandle& pipeline,
    const GpuGraphPipelineDesc& desc
){
    if(!pipeline || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphPipelineType::Meshlet)
        return {};

    for(usize pipelineIndex = 0u; pipelineIndex < m_pipelines.size(); ++pipelineIndex){
        const GpuGraphPipelineNode& existing = m_pipelines[pipelineIndex];
        if(existing.type == GpuGraphPipelineType::Meshlet && existing.meshletPipeline.get() == pipeline.get()){
            if(!__hidden_gpu_task_graph_imports::CompatiblePipelineMetadata(pipelineAt(pipelineIndex), desc))
                return {};
            return GpuGraphPipelineId{ static_cast<u32>(pipelineIndex), m_generation };
        }
        if(existing.identity == desc.identity)
            return {};
    }

    const GpuGraphPipelineId id = appendPipeline(desc);
    if(id.valid()){
        GpuGraphPipelineNode& importedPipeline = m_pipelines[id.index];
        importedPipeline.meshletPipeline = pipeline;
        importedPipeline.deviceGeneration = pipeline->getDeviceGeneration();
    }
    return id;
}

GpuGraphPipelineId GpuTaskGraph::importRayTracingPipeline(
    const RayTracingPipelineHandle& pipeline,
    const GpuGraphPipelineDesc& desc
){
    if(!pipeline || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphPipelineType::RayTracing)
        return {};

    for(usize pipelineIndex = 0u; pipelineIndex < m_pipelines.size(); ++pipelineIndex){
        const GpuGraphPipelineNode& existing = m_pipelines[pipelineIndex];
        if(existing.type == GpuGraphPipelineType::RayTracing && existing.rayTracingPipeline.get() == pipeline.get()){
            if(!__hidden_gpu_task_graph_imports::CompatiblePipelineMetadata(pipelineAt(pipelineIndex), desc))
                return {};
            return GpuGraphPipelineId{ static_cast<u32>(pipelineIndex), m_generation };
        }
        if(existing.identity == desc.identity)
            return {};
    }

    const GpuGraphPipelineId id = appendPipeline(desc);
    if(id.valid()){
        GpuGraphPipelineNode& importedPipeline = m_pipelines[id.index];
        importedPipeline.rayTracingPipeline = pipeline;
        importedPipeline.deviceGeneration = pipeline->getDeviceGeneration();
    }
    return id;
}

GpuExternalCompletionId GpuTaskGraph::importExternalCompletion(const GpuExternalCompletionDesc& desc){
    const bool hasToken = __hidden_gpu_task_graph_imports::HasExternalCompletionTokenValue(desc.token);
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || (hasToken && (
            !desc.token.valid()
            || !desc.token.hasPhysicalQueueIdentity()
            || !validForDeviceGeneration(desc.token.deviceGeneration)
        ))
    )
        return {};

    for(usize completionIndex = 0u; completionIndex < m_externalCompletions.size(); ++completionIndex){
        GpuExternalCompletionNode& existing = m_externalCompletions[completionIndex];
        if(existing.identity != desc.identity)
            continue;
        if(hasToken){
            if(existing.hasToken){
                if(!__hidden_gpu_task_graph_imports::SameSubmissionToken(existing.token, desc.token))
                    return {};
            }
            else{
                existing.token = desc.token;
                existing.hasToken = true;
                m_declarationRevision = allocateGeneration();
            }
        }
        return GpuExternalCompletionId{ static_cast<u32>(completionIndex), m_generation };
    }

    return appendExternalCompletion(desc);
}

bool GpuTaskGraph::declarePresentEndpoint(const GpuPresentEndpoint& endpoint){
    if(
        m_hasPresentEndpoint
        || !validTask(endpoint.producer)
        || !validResource(endpoint.backBuffer)
    )
        return false;

    m_presentEndpoint = endpoint;
    m_declarationRevision = allocateGeneration();
    m_hasPresentEndpoint = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

