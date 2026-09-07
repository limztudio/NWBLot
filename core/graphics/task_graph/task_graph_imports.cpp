// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include <core/graphics/backend_selection.h>

#include <global/allocation_size.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_imports{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CompatibleResourceMetadata(
    const GpuTaskGraphResourceView& resource,
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
    const bool initialOwnerStateEquivalent =
        (!resource.initialOwnerStateSource && !desc.initialOwnerStateSource)
        || (
            resource.initialOwnerStateSource
            && desc.initialOwnerStateSource
            && resource.initialOwnerStateSource->equivalentTo(*desc.initialOwnerStateSource)
        )
    ;
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
        && initialOwnerStateEquivalent
        && resource.queueSharing == desc.queueSharing
        && resource.initialAvailabilityCompletion == desc.initialAvailabilityCompletion
    ;
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

[[nodiscard]] static bool CompatibleQueueAdmission(
    const ResourceQueueAdmissionSnapshot& admission,
    const ResourceQueueSharing::Mask logicalSharing,
    const usize retainedQueueFamilyIndexCount
)noexcept{
    if(
        !admission.valid()
        || admission.admittedQueueClasses != logicalSharing
        || retainedQueueFamilyIndexCount > static_cast<usize>(Limit<u32>::s_Max)
    )
        return false;
    return admission.queueFamilyIndexCount
        <= static_cast<usize>(Limit<u32>::s_Max) - retainedQueueFamilyIndexCount
    ;
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


usize GpuTaskGraph::ResourcePointerHasher::operator()(const ResourcePointerKey& key)const noexcept{
    usize hash = Hasher<const void*>{}(key.pointer);
    HashCombine(hash, static_cast<u8>(key.type));
    return hash;
}

GpuTaskGraph::ResourcePointerKey GpuTaskGraph::resourcePointerKey(const GpuGraphResourceNode& resource)noexcept{
    switch(resource.type){
    case GpuGraphResourceType::Texture:
        return { resource.texture.get(), resource.type };
    case GpuGraphResourceType::Buffer:
        return { resource.buffer.get(), resource.type };
    case GpuGraphResourceType::AccelStruct:
        return { resource.accelStruct.get(), resource.type };
    default:
        return {};
    }
}

GpuGraphResourceId GpuTaskGraph::importResource(const GpuGraphResourceDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(!desc.identity || desc.markerLabel.empty() || desc.type >= GpuGraphResourceType::kCount)
        return {};

    const u32 resourceIndex = findResourceIdentity(desc.identity);
    if(resourceIndex != s_InvalidImportIndex){
        if(!__hidden_gpu_task_graph_imports::CompatibleResourceMetadata(resourceAt(resourceIndex), desc))
            return {};
        return GpuGraphResourceId{ resourceIndex, m_generation };
    }

    return appendResourceWithinMutation(desc, nullptr, {}, mutation);
}

GpuGraphResourceId GpuTaskGraph::importTexture(const TextureHandle& texture, const GpuGraphResourceDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(!texture || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphResourceType::Texture)
        return {};
    if(!texture->descriptionMatchesCreation())
        return {};

    const TextureDesc& textureDesc = texture->getCreationDescription();
    const ResourceQueueAdmissionSnapshot queueAdmission = texture->getQueueAdmissionSnapshot();
    if(!__hidden_gpu_task_graph_imports::CompatibleQueueAdmission(
        queueAdmission,
        textureDesc.queueSharing,
        m_queueFamilyIndices.size()
    ))
        return {};
    if(!__hidden_gpu_task_graph_imports::CompatibleRetainedExternalFinalState(
        textureDesc.keepInitialState,
        textureDesc.initialState,
        desc.externalFinalState
    ))
        return {};

    GpuGraphResourceDesc resolvedDesc = desc;
    if(!resolvedDesc.hasExplicitInitialState && resolvedDesc.initialState == ResourceStates::Unknown){
        // The backend preserves managed-fresh descriptor semantics while retaining Unknown for native or partially
        // published textures whose current state is not established by their creation description.
        resolvedDesc.initialState = texture->resolveTaskGraphImportInitialState();
    }
    if(resolvedDesc.queueSharing != ResourceQueueSharing::Exclusive && resolvedDesc.queueSharing != textureDesc.queueSharing)
        return {};
    resolvedDesc.queueSharing = textureDesc.queueSharing;

    const ResourceImportMatch match = findResourceImportMatch(
        &resolvedDesc.identity.identityHash(), { texture.get(), GpuGraphResourceType::Texture }
    );
    if(match.index != s_InvalidImportIndex){
        if(
            !match.samePointer
            || !__hidden_gpu_task_graph_imports::CompatibleResourceMetadata(resourceAt(match.index), resolvedDesc)
        )
            return {};
        return GpuGraphResourceId{ match.index, m_generation };
    }

    return appendResourceWithinMutation(
        resolvedDesc,
        &queueAdmission,
        ResourceBinding{ .texture = &texture, .deviceGeneration = texture->getDeviceGeneration() },
        mutation
    );
}

GpuGraphResourceId GpuTaskGraph::importBuffer(const BufferHandle& buffer, const GpuGraphResourceDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(
        !buffer
        || !buffer->descriptionMatchesCreation()
        || !desc.identity
        || desc.markerLabel.empty()
        || desc.type != GpuGraphResourceType::Buffer
    )
        return {};

    const BufferDesc& bufferDesc = buffer->getCreationDescription();
    const ResourceQueueAdmissionSnapshot queueAdmission = buffer->getQueueAdmissionSnapshot();
    if(!__hidden_gpu_task_graph_imports::CompatibleQueueAdmission(
        queueAdmission,
        bufferDesc.queueSharing,
        m_queueFamilyIndices.size()
    ))
        return {};
    if(!__hidden_gpu_task_graph_imports::CompatibleRetainedExternalFinalState(
        bufferDesc.keepInitialState,
        bufferDesc.initialState,
        desc.externalFinalState
    ))
        return {};

    GpuGraphResourceDesc resolvedDesc = desc;
    if(!resolvedDesc.hasExplicitInitialState && resolvedDesc.initialState == ResourceStates::Unknown)
        resolvedDesc.initialState = buffer->resolveTaskGraphImportInitialState();
    if(resolvedDesc.queueSharing != ResourceQueueSharing::Exclusive && resolvedDesc.queueSharing != bufferDesc.queueSharing)
        return {};
    resolvedDesc.queueSharing = bufferDesc.queueSharing;

    const ResourceImportMatch match = findResourceImportMatch(
        &resolvedDesc.identity.identityHash(), { buffer.get(), GpuGraphResourceType::Buffer }
    );
    if(match.index != s_InvalidImportIndex){
        if(
            !match.samePointer
            || !__hidden_gpu_task_graph_imports::CompatibleResourceMetadata(resourceAt(match.index), resolvedDesc)
        )
            return {};
        return GpuGraphResourceId{ match.index, m_generation };
    }

    return appendResourceWithinMutation(
        resolvedDesc,
        &queueAdmission,
        ResourceBinding{ .buffer = &buffer, .deviceGeneration = buffer->getDeviceGeneration() },
        mutation
    );
}

GpuGraphResourceId GpuTaskGraphDeclarationReadView::findImportedTexture(const TextureHandle& texture)const noexcept{
    if(!m_graph || !texture)
        return {};
    const auto match = m_graph->findResourceImportMatch(nullptr, { texture.get(), GpuGraphResourceType::Texture });
    return match.index == GpuTaskGraph::s_InvalidImportIndex
        ? GpuGraphResourceId{}
        : GpuGraphResourceId{ match.index, m_graph->m_generation };
}

GpuGraphResourceId GpuTaskGraphDeclarationReadView::findImportedBuffer(const BufferHandle& buffer)const noexcept{
    if(!m_graph || !buffer)
        return {};
    const auto match = m_graph->findResourceImportMatch(nullptr, { buffer.get(), GpuGraphResourceType::Buffer });
    return match.index == GpuTaskGraph::s_InvalidImportIndex
        ? GpuGraphResourceId{}
        : GpuGraphResourceId{ match.index, m_graph->m_generation };
}

const GpuPresentEndpoint* GpuTaskGraphDeclarationReadView::presentEndpoint()const & noexcept{
    return m_graph && m_graph->m_hasPresentEndpoint ? &m_graph->m_presentEndpoint : nullptr;
}

GpuGraphResourceId GpuTaskGraph::importAccelStruct(
    const RayTracingAccelStructHandle& accelStruct,
    const GpuGraphResourceDesc& desc
){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(!accelStruct || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphResourceType::AccelStruct)
        return {};

    const ResourceQueueSharing::Mask creationQueueSharing = accelStruct->getCreationQueueSharing();
    if(!accelStruct->queueSharingMatchesCreation())
        return {};
    const Buffer* const backingBuffer = accelStruct->getBackingBuffer();
    ResourceQueueAdmissionSnapshot queueAdmission;
    if(backingBuffer){
        const BufferDesc& backingBufferDesc = backingBuffer->getCreationDescription();
        queueAdmission = backingBuffer->getQueueAdmissionSnapshot();
        if(
            !backingBuffer->descriptionMatchesCreation()
            || backingBufferDesc.queueSharing != creationQueueSharing
            || !__hidden_gpu_task_graph_imports::CompatibleQueueAdmission(
                queueAdmission,
                creationQueueSharing,
                m_queueFamilyIndices.size()
            )
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
        backingBuffer
        && !resolvedDesc.hasExplicitInitialState
        && resolvedDesc.initialState == ResourceStates::Unknown
    )
        resolvedDesc.initialState = backingBuffer->resolveTaskGraphImportInitialState();
    if(
        resolvedDesc.queueSharing != ResourceQueueSharing::Exclusive
        && resolvedDesc.queueSharing != creationQueueSharing
    )
        return {};
    resolvedDesc.queueSharing = creationQueueSharing;

    const ResourceImportMatch match = findResourceImportMatch(
        &resolvedDesc.identity.identityHash(), { accelStruct.get(), GpuGraphResourceType::AccelStruct }
    );
    if(match.index != s_InvalidImportIndex){
        if(
            !match.samePointer
            || !__hidden_gpu_task_graph_imports::CompatibleResourceMetadata(resourceAt(match.index), resolvedDesc)
        )
            return {};
        return GpuGraphResourceId{ match.index, m_generation };
    }

    return appendResourceWithinMutation(
        resolvedDesc,
        backingBuffer ? &queueAdmission : nullptr,
        ResourceBinding{ .accelStruct = &accelStruct, .deviceGeneration = accelStruct->getDeviceGeneration() },
        mutation
    );
}

GpuGraphResourceId GpuTaskGraph::importHazardDomain(const GpuGraphResourceDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(desc.type != GpuGraphResourceType::HazardDomain)
        return {};
    return importResource(desc);
}

GpuGraphResourceSetId GpuTaskGraph::importResourceSet(const GpuGraphResourceSetDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(
        !desc.identity
        || desc.markerLabel.empty()
        || (desc.memberCount != 0u && !desc.members)
    )
        return {};

    const u32 resourceSetIndex = findResourceSetIdentity(desc.identity);
    if(resourceSetIndex != s_InvalidImportIndex){
        const GpuTaskGraphResourceSetView existing = resourceSetAt(resourceSetIndex);
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
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

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
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

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
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

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
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

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
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

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
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

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
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return false;

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


u32 GpuTaskGraph::findResourceIdentity(const Name& identity)const noexcept{
    const NameHash& key = identity.identityHash();
    if(m_resourceIdentityIndex){
        const auto found = m_resourceIdentityIndex->find(key);
        return found == m_resourceIdentityIndex->end() ? s_InvalidImportIndex : found.value();
    }
    for(usize index = 0u; index < m_resources.size(); ++index){
        if(m_resources[index].identity.identityHash() == key)
            return static_cast<u32>(index);
    }
    return s_InvalidImportIndex;
}

GpuTaskGraph::ResourceImportMatch GpuTaskGraph::findResourceImportMatch(
    const NameHash* const identity,
    const ResourcePointerKey& pointer
)const noexcept{
    if(!pointer.pointer)
        return {};
    if(m_resourceIdentityIndex){
        ResourceImportMatch match;
        if(m_resourcePointerIndex){
            const auto found = m_resourcePointerIndex->find(pointer);
            if(found != m_resourcePointerIndex->end())
                match = { found.value(), true };
        }
        if(identity){
            const auto found = m_resourceIdentityIndex->find(*identity);
            // At the same ordinal the typed pointer wins. An earlier identity conflict still rejects before a
            // later pointer match, matching the original ordered import validation.
            if(found != m_resourceIdentityIndex->end() && found.value() < match.index)
                return { found.value(), false };
        }
        return match;
    }
    for(usize index = 0u; index < m_resources.size(); ++index){
        const GpuGraphResourceNode& resource = m_resources[index];
        if(resource.type == pointer.type){
            const void* typedPointer = nullptr;
            switch(resource.type){
            case GpuGraphResourceType::Texture:
                typedPointer = resource.texture.get();
                break;
            case GpuGraphResourceType::Buffer:
                typedPointer = resource.buffer.get();
                break;
            case GpuGraphResourceType::AccelStruct:
                typedPointer = resource.accelStruct.get();
                break;
            default:
                break;
            }
            if(typedPointer == pointer.pointer)
                return { static_cast<u32>(index), true };
        }
        if(identity && resource.identity.identityHash() == *identity)
            return { static_cast<u32>(index), false };
    }
    return {};
}

u32 GpuTaskGraph::findResourceSetIdentity(const Name& identity)const noexcept{
    const NameHash& key = identity.identityHash();
    if(m_resourceSetIdentityIndex){
        const auto found = m_resourceSetIdentityIndex->find(key);
        return found == m_resourceSetIdentityIndex->end() ? s_InvalidImportIndex : found.value();
    }
    for(usize index = 0u; index < m_resourceSets.size(); ++index){
        if(m_resourceSets[index].identity.identityHash() == key)
            return static_cast<u32>(index);
    }
    return s_InvalidImportIndex;
}

void GpuTaskGraph::prepareResourceIndexes(const ResourcePointerKey& pendingPointer){
    if(!m_resourceIdentityIndex){
        if(m_resources.size() < s_InlineImportIndexCount)
            return;

        const usize identityCount = AddSize(m_resources.size(), 1u);
        ResourceIdentityIndex identities(AddSize(identityCount, identityCount), m_arena);
        usize pointerCount = pendingPointer.pointer ? 1u : 0u;
        for(usize index = 0u; index < m_resources.size(); ++index){
            const GpuGraphResourceNode& resource = m_resources[index];
            // Insertion retains the first ordinal; later declarations must never replace earlier lookup priority.
            identities.emplace(resource.identity.identityHash(), static_cast<u32>(index));
            if(resourcePointerKey(resource).pointer)
                ++pointerCount;
        }
        Optional<ResourcePointerIndex> pointers;
        if(pointerCount != 0u){
            pointers.emplace(AddSize(pointerCount, pointerCount), m_arena);
            for(usize index = 0u; index < m_resources.size(); ++index){
                const ResourcePointerKey key = resourcePointerKey(m_resources[index]);
                if(key.pointer)
                    pointers->emplace(key, static_cast<u32>(index));
            }
        }

        // Build both tables before publishing either. Their move construction cannot fail, and both contain only
        // already-published ordinals; an unsuccessful later append may retain capacity without changing lookup.
        static_assert(IsNothrowMoveConstructible_V<ResourceIdentityIndex>);
        static_assert(IsNothrowMoveConstructible_V<ResourcePointerIndex>);
        m_resourceIdentityIndex.emplace(Move(identities));
        if(pointers)
            m_resourcePointerIndex.emplace(Move(*pointers));
    }
    else if(pendingPointer.pointer && !m_resourcePointerIndex)
        m_resourcePointerIndex.emplace(2u, m_arena);
}

void GpuTaskGraph::prepareResourceSetIndex(){
    if(m_resourceSetIdentityIndex || m_resourceSets.size() < s_InlineImportIndexCount)
        return;
    const usize identityCount = AddSize(m_resourceSets.size(), 1u);
    ResourceIdentityIndex identities(AddSize(identityCount, identityCount), m_arena);
    // emplace intentionally keeps the first ordinal for the exact identity.
    for(usize index = 0u; index < m_resourceSets.size(); ++index)
        identities.emplace(m_resourceSets[index].identity.identityHash(), static_cast<u32>(index));
    static_assert(IsNothrowMoveConstructible_V<ResourceIdentityIndex>);
    m_resourceSetIdentityIndex.emplace(Move(identities));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

