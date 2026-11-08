// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include <core/graphics/backend_selection/backend.h>

#include <global/allocation_size.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_imports{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 CompatibleResourceMetadata(const GpuTaskGraphResourceView& resource, const GpuGraphResourceDesc& desc)noexcept{
    // Re-importing an ownership source through the ordinary typed-import overload could silently exchange one of its immutable ownership snapshots.
    // Require callers to reuse its graph resource ID instead.
    if(resource.initialOwnerHandoffSourceCount != 0u || desc.initialOwnerHandoffSources || desc.initialOwnerHandoffSourceCount != 0u)
        return false;
    return
        resource.identity == desc.identity
        && resource.type == desc.type
        && resource.initialState == desc.initialState
        && resource.externalFinalState == desc.externalFinalState
        && resource.externalFinalReleaseDestinationQueue == desc.externalFinalReleaseDestinationQueue
        && resource.initialOwnerQueue == desc.initialOwnerQueue
        && resource.queueSharing == desc.queueSharing
        && resource.initialAvailabilityCompletion == desc.initialAvailabilityCompletion
        && resource.directConsumerQueue == desc.directConsumerQueue
    ;
}

[[nodiscard]] static b8 CompatibleRetainedExternalFinalState(const b8 keepInitialState, const ResourceStates::Mask nativeInitialState, const ResourceStates::Mask externalFinalState)noexcept{
    // Native command-list close restores retained resources to their descriptor state.
    // A typed graph import must not publish a different known terminal state after that restoration.
    return !keepInitialState || externalFinalState == ResourceStates::Unknown || externalFinalState == nativeInitialState;
}

[[nodiscard]] static b8 CompatibleQueueAdmission(const ResourceQueueAdmissionSnapshot& admission, const ResourceQueueSharing::Mask logicalSharing, const usize retainedQueueFamilyIndexCount)noexcept{
    if(!admission.valid() || admission.admittedQueueClasses != logicalSharing || retainedQueueFamilyIndexCount > static_cast<usize>(Limit<u32>::s_Max))
        return false;
    return admission.queueFamilyIndexCount <= static_cast<usize>(Limit<u32>::s_Max) - retainedQueueFamilyIndexCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<GpuGraphResourceId> GpuTaskGraph::importResource(const GpuGraphResourceDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return MakeUnexpected(Failure{});

    if(!desc.identity || desc.markerLabel.empty() || desc.type >= GpuGraphResourceType::Count)
        return MakeUnexpected(Failure{});

    const u32 resourceIndex = m_resourceImportIndexes.findResourceIdentity(m_resources, desc.identity);
    if(resourceIndex != s_InvalidImportIndex){
        if(!__hidden_gpu_task_graph_imports::CompatibleResourceMetadata(resourceAt(resourceIndex), desc))
            return MakeUnexpected(Failure{});
        return GpuGraphResourceId{ .generation = m_generation, .index = resourceIndex };
    }

    return appendResourceWithinMutation(desc, nullptr, {}, mutation);
}

Expected<GpuGraphResourceId> GpuTaskGraph::importTexture(const TextureHandle& texture, const GpuGraphResourceDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return MakeUnexpected(Failure{});

    if(!texture || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphResourceType::Texture)
        return MakeUnexpected(Failure{});
    if(!texture->descriptionMatchesCreation())
        return MakeUnexpected(Failure{});

    const TextureDesc& textureDesc = texture->getCreationDescription();
    const ResourceQueueAdmissionSnapshot queueAdmission = texture->getQueueAdmissionSnapshot();
    if(!__hidden_gpu_task_graph_imports::CompatibleQueueAdmission(queueAdmission, textureDesc.queueSharing, m_queueFamilyIndices.size()))
        return MakeUnexpected(Failure{});
    if(!__hidden_gpu_task_graph_imports::CompatibleRetainedExternalFinalState(textureDesc.keepInitialState, textureDesc.initialState, desc.externalFinalState))
        return MakeUnexpected(Failure{});

    GpuGraphResourceDesc resolvedDesc = desc;
    if(!resolvedDesc.hasExplicitInitialState && resolvedDesc.initialState == ResourceStates::Unknown){
        // Managed-fresh descriptors keep Unknown for native textures whose state the creation desc never set.
        resolvedDesc.initialState = texture->resolveTaskGraphImportInitialState();
    }
    if(resolvedDesc.queueSharing != ResourceQueueSharing::Exclusive && resolvedDesc.queueSharing != textureDesc.queueSharing)
        return MakeUnexpected(Failure{});
    resolvedDesc.queueSharing = textureDesc.queueSharing;

    const ResourceImportMatch match = m_resourceImportIndexes.findResourceImportMatch(m_resources, &resolvedDesc.identity.identityHash(), { texture.get(), GpuGraphResourceType::Texture });
    if(match.index != s_InvalidImportIndex){
        if(!match.samePointer || !__hidden_gpu_task_graph_imports::CompatibleResourceMetadata(resourceAt(match.index), resolvedDesc))
            return MakeUnexpected(Failure{});
        return GpuGraphResourceId{ .generation = m_generation, .index = match.index };
    }

    return appendResourceWithinMutation(resolvedDesc, &queueAdmission, ResourceBinding{ .texture = &texture, .deviceGeneration = texture->getDeviceGeneration() }, mutation);
}

Expected<GpuGraphResourceId> GpuTaskGraph::importBuffer(const BufferHandle& buffer, const GpuGraphResourceDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return MakeUnexpected(Failure{});

    if(!buffer || !buffer->descriptionMatchesCreation() || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphResourceType::Buffer)
        return MakeUnexpected(Failure{});

    const BufferDesc& bufferDesc = buffer->getCreationDescription();
    const ResourceQueueAdmissionSnapshot queueAdmission = buffer->getQueueAdmissionSnapshot();
    if(!__hidden_gpu_task_graph_imports::CompatibleQueueAdmission(queueAdmission, bufferDesc.queueSharing, m_queueFamilyIndices.size()))
        return MakeUnexpected(Failure{});
    if(!__hidden_gpu_task_graph_imports::CompatibleRetainedExternalFinalState(bufferDesc.keepInitialState, bufferDesc.initialState, desc.externalFinalState))
        return MakeUnexpected(Failure{});

    GpuGraphResourceDesc resolvedDesc = desc;
    if(!resolvedDesc.hasExplicitInitialState && resolvedDesc.initialState == ResourceStates::Unknown)
        resolvedDesc.initialState = buffer->resolveTaskGraphImportInitialState();
    if(resolvedDesc.queueSharing != ResourceQueueSharing::Exclusive && resolvedDesc.queueSharing != bufferDesc.queueSharing)
        return MakeUnexpected(Failure{});
    resolvedDesc.queueSharing = bufferDesc.queueSharing;

    const ResourceImportMatch match = m_resourceImportIndexes.findResourceImportMatch(m_resources, &resolvedDesc.identity.identityHash(), { buffer.get(), GpuGraphResourceType::Buffer });
    if(match.index != s_InvalidImportIndex){
        if(!match.samePointer || !__hidden_gpu_task_graph_imports::CompatibleResourceMetadata(resourceAt(match.index), resolvedDesc))
            return MakeUnexpected(Failure{});
        return GpuGraphResourceId{ .generation = m_generation, .index = match.index };
    }

    return appendResourceWithinMutation(resolvedDesc, &queueAdmission, ResourceBinding{ .buffer = &buffer, .deviceGeneration = buffer->getDeviceGeneration() }, mutation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuGraphResourceId GpuTaskGraphDeclarationReadView::findImportedTexture(const TextureHandle& texture)const noexcept{
    if(!m_graph || !texture)
        return {};
    const auto match = m_graph->m_resourceImportIndexes.findResourceImportMatch(m_graph->m_resources, nullptr, { texture.get(), GpuGraphResourceType::Texture });
    return match.index == GpuTaskGraph::s_InvalidImportIndex ? GpuGraphResourceId{} : GpuGraphResourceId{ .generation = m_graph->m_generation, .index = match.index };
}

GpuGraphResourceId GpuTaskGraphDeclarationReadView::findImportedBuffer(const BufferHandle& buffer)const noexcept{
    if(!m_graph || !buffer)
        return {};
    const auto match = m_graph->m_resourceImportIndexes.findResourceImportMatch(m_graph->m_resources, nullptr, { buffer.get(), GpuGraphResourceType::Buffer });
    return match.index == GpuTaskGraph::s_InvalidImportIndex ? GpuGraphResourceId{} : GpuGraphResourceId{ .generation = m_graph->m_generation, .index = match.index };
}

const GpuPresentEndpoint* GpuTaskGraphDeclarationReadView::presentEndpoint()const & noexcept{
    return m_graph && m_graph->m_hasPresentEndpoint ? &m_graph->m_presentEndpoint : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<GpuGraphResourceId> GpuTaskGraph::importAccelStruct(const RayTracingAccelStructHandle& accelStruct, const GpuGraphResourceDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return MakeUnexpected(Failure{});

    if(!accelStruct || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphResourceType::AccelStruct)
        return MakeUnexpected(Failure{});

    const ResourceQueueSharing::Mask creationQueueSharing = accelStruct->getCreationQueueSharing();
    if(!accelStruct->queueSharingMatchesCreation())
        return MakeUnexpected(Failure{});
    const Buffer* const backingBuffer = accelStruct->getBackingBuffer();
    ResourceQueueAdmissionSnapshot queueAdmission;
    if(backingBuffer){
        const BufferDesc& backingBufferDesc = backingBuffer->getCreationDescription();
        queueAdmission = backingBuffer->getQueueAdmissionSnapshot();
        if(
            !backingBuffer->descriptionMatchesCreation()
            || backingBufferDesc.queueSharing != creationQueueSharing
            || !__hidden_gpu_task_graph_imports::CompatibleQueueAdmission(queueAdmission, creationQueueSharing, m_queueFamilyIndices.size())
            || !__hidden_gpu_task_graph_imports::CompatibleRetainedExternalFinalState(backingBufferDesc.keepInitialState, backingBufferDesc.initialState, desc.externalFinalState)
        )
            return MakeUnexpected(Failure{});
    }

    GpuGraphResourceDesc resolvedDesc = desc;
    if(backingBuffer && !resolvedDesc.hasExplicitInitialState && resolvedDesc.initialState == ResourceStates::Unknown)
        resolvedDesc.initialState = backingBuffer->resolveTaskGraphImportInitialState();
    if(resolvedDesc.queueSharing != ResourceQueueSharing::Exclusive && resolvedDesc.queueSharing != creationQueueSharing)
        return MakeUnexpected(Failure{});
    resolvedDesc.queueSharing = creationQueueSharing;

    const ResourceImportMatch match = m_resourceImportIndexes.findResourceImportMatch(m_resources, &resolvedDesc.identity.identityHash(), { accelStruct.get(), GpuGraphResourceType::AccelStruct });
    if(match.index != s_InvalidImportIndex){
        if(!match.samePointer || !__hidden_gpu_task_graph_imports::CompatibleResourceMetadata(resourceAt(match.index), resolvedDesc))
            return MakeUnexpected(Failure{});
        return GpuGraphResourceId{ .generation = m_generation, .index = match.index };
    }

    return appendResourceWithinMutation(resolvedDesc, backingBuffer ? &queueAdmission : nullptr, ResourceBinding{ .accelStruct = &accelStruct, .deviceGeneration = accelStruct->getDeviceGeneration() }, mutation);
}

Expected<GpuGraphResourceId> GpuTaskGraph::importHazardDomain(const GpuGraphResourceDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return MakeUnexpected(Failure{});

    if(desc.type != GpuGraphResourceType::HazardDomain)
        return MakeUnexpected(Failure{});
    return importResource(desc);
}

Expected<GpuGraphResourceSetId> GpuTaskGraph::importResourceSet(const GpuGraphResourceSetDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return MakeUnexpected(Failure{});

    if(!desc.identity || desc.markerLabel.empty() || (desc.memberCount != 0u && !desc.members))
        return MakeUnexpected(Failure{});

    const u32 resourceSetIndex = m_resourceImportIndexes.findResourceSetIdentity(m_resourceSets, desc.identity);
    if(resourceSetIndex != s_InvalidImportIndex){
        const GpuTaskGraphResourceSetView existing = resourceSetAt(resourceSetIndex);
        if(existing.memberCount != desc.memberCount)
            return MakeUnexpected(Failure{});
        for(usize memberIndex = 0u; memberIndex < desc.memberCount; ++memberIndex){
            if(existing.members[memberIndex] != desc.members[memberIndex])
                return MakeUnexpected(Failure{});
        }
        return existing.id;
    }

    return appendResourceSet(desc);
}

b8 GpuTaskGraph::declarePresentEndpoint(const GpuPresentEndpoint& endpoint){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return false;

    if(m_hasPresentEndpoint || !validTask(endpoint.producer) || !validResource(endpoint.backBuffer))
        return false;

    m_presentEndpoint = endpoint;
    m_declarationRevision = AllocateGeneration();
    m_hasPresentEndpoint = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

