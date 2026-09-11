// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiler.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraph::validTask(const GpuTaskId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_tasks.size();
}

bool GpuTaskGraph::validResource(const GpuGraphResourceId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_resources.size();
}

bool GpuTaskGraph::validResourceVersion(const GpuGraphResourceVersionId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_resourceVersions.size();
}

bool GpuTaskGraph::validResourceSet(const GpuGraphResourceSetId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_resourceSets.size();
}

bool GpuTaskGraph::validUploadBlob(const GpuUploadBlobId& id)const noexcept{
    return id.valid()
        && id.generation == m_generation
        && id.index < m_uploadBlobs.size()
        && !m_uploadBlobs[id.index].bytes.empty()
    ;
}

bool GpuTaskGraph::validPipeline(const GpuGraphPipelineId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_pipelines.size();
}

bool GpuTaskGraph::validExternalCompletion(const GpuExternalCompletionId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_externalCompletions.size();
}

GpuTaskGraphTaskView GpuTaskGraph::taskAt(const usize index)const{
    NWB_ASSERT(index < m_tasks.size());
    const GpuTaskNode& task = m_tasks[index];
    return GpuTaskGraphTaskView{
        .id = GpuTaskId{ static_cast<u32>(index), m_generation },
        .identity = task.identity,
        .markerLabel = markerLabel(task.markerLabelOffset, task.markerLabelSize),
        .queue = task.queue,
        .scheduling = task.scheduling,
        .timing = task.timing,
        .dependencies = task.dependencyCount > 0u ? m_dependencies.data() + task.dependencyOffset : nullptr,
        .dependencyCount = task.dependencyCount,
        .externalDependencies = task.externalDependencyCount > 0u
            ? m_externalDependencies.data() + task.externalDependencyOffset
            : nullptr,
        .externalDependencyCount = task.externalDependencyCount,
        .externalStateSources = task.externalStateSourceCount > 0u
            ? m_externalStateSources.data() + task.externalStateSourceOffset
            : nullptr,
        .externalStateSourceCount = task.externalStateSourceCount,
        .resourceUses = task.resourceUseCount > 0u ? m_resourceUses.data() + task.resourceUseOffset : nullptr,
        .resourceUseCount = task.resourceUseCount,
        .resourceVersionUses = task.resourceVersionUseCount > 0u
            ? m_resourceVersionUses.data() + task.resourceVersionUseOffset
            : nullptr,
        .resourceVersionUseCount = task.resourceVersionUseCount,
        .payloadObjectSize = task.payloadObjectSize,
        .directResourceUseCount = task.directResourceUseCount,
        .declaredResourceSetUseCount = task.declaredResourceSetUseCount,
        .expandedResourceSetMemberUseCount = task.expandedResourceSetMemberUseCount,
        .hasPayload = task.payload != nullptr,
        .hasRecordPayload = task.recordPayload != nullptr,
        .hasAcceptedPayload = task.acceptPayload != nullptr,
    };
}

GpuTaskGraphResourceView GpuTaskGraph::resourceAt(const usize index)const{
    NWB_ASSERT(index < m_resources.size());
    const GpuGraphResourceNode& resource = m_resources[index];
    return GpuTaskGraphResourceView{
        .id = GpuGraphResourceId{ static_cast<u32>(index), m_generation },
        .identity = resource.identity,
        .markerLabel = markerLabel(resource.markerLabelOffset, resource.markerLabelSize),
        .initialState = resource.initialState,
        .externalFinalState = resource.externalFinalState,
        .externalFinalReleaseDestinationQueue = resource.externalFinalReleaseDestinationQueue,
        .initialOwnerQueue = resource.initialOwnerQueue,
        .initialOwnerReleaseDestinationQueue = resource.initialOwnerReleaseDestinationQueue,
        .initialOwnerCompletion = resource.initialOwnerCompletion,
        .initialOwnerMinimumCompletionToken = resource.initialOwnerMinimumCompletionToken,
        .initialOwnerStateSource = resource.initialOwnerStateSource,
        .initialOwnerHandoffSources = resource.initialOwnerHandoffSourceCount != 0u
            ? m_initialOwnerHandoffSources.data() + resource.initialOwnerHandoffSourceOffset
            : nullptr,
        .initialOwnerHandoffSourceCount = resource.initialOwnerHandoffSourceCount,
        .initialAvailabilityCompletion = resource.initialAvailabilityCompletion,
        .queueAdmission = ResourceQueueAdmissionSnapshot{
            .queueFamilyIndices = resource.queueFamilyIndexCount != 0u
                ? m_queueFamilyIndices.data() + resource.queueFamilyIndexOffset
                : nullptr,
            .queueFamilyIndexCount = resource.queueFamilyIndexCount,
            .admittedQueueClasses = resource.queueSharing,
            .usesConcurrentSharing = resource.usesConcurrentSharing,
        },
        .type = resource.type,
        .queueSharing = resource.queueSharing,
        .hasQueueAdmission = resource.hasQueueAdmission,
        .hasBackendResource = resource.texture != nullptr || resource.buffer != nullptr || resource.accelStruct != nullptr,
    };
}

GpuTaskGraphResourceVersionView GpuTaskGraph::resourceVersionAt(const usize index)const{
    NWB_ASSERT(index < m_resourceVersions.size());
    const GpuGraphResourceVersionNode& version = m_resourceVersions[index];
    return GpuTaskGraphResourceVersionView{
        .id = GpuGraphResourceVersionId{ static_cast<u32>(index), m_generation },
        .resource = version.resource,
        .range = version.range,
        .origin = version.origin,
    };
}

GpuTaskGraphResourceSetView GpuTaskGraph::resourceSetAt(const usize index)const{
    NWB_ASSERT(index < m_resourceSets.size());
    const GpuGraphResourceSetNode& resourceSet = m_resourceSets[index];
    return GpuTaskGraphResourceSetView{
        .id = GpuGraphResourceSetId{ static_cast<u32>(index), m_generation },
        .identity = resourceSet.identity,
        .markerLabel = markerLabel(resourceSet.markerLabelOffset, resourceSet.markerLabelSize),
        .members = resourceSet.memberCount > 0u ? m_resourceSetMembers.data() + resourceSet.memberOffset : nullptr,
        .memberCount = resourceSet.memberCount,
    };
}

GpuTaskGraphPipelineView GpuTaskGraph::pipelineAt(const usize index)const{
    NWB_ASSERT(index < m_pipelines.size());
    const GpuGraphPipelineNode& pipeline = m_pipelines[index];
    return GpuTaskGraphPipelineView{
        .id = GpuGraphPipelineId{ static_cast<u32>(index), m_generation },
        .identity = pipeline.identity,
        .markerLabel = markerLabel(pipeline.markerLabelOffset, pipeline.markerLabelSize),
        .type = pipeline.type,
        .hasBackendPipeline = pipeline.graphicsPipeline != nullptr
            || pipeline.computePipeline != nullptr
            || pipeline.meshletPipeline != nullptr
            || pipeline.rayTracingPipeline != nullptr,
    };
}

GpuTaskGraphExternalCompletionView GpuTaskGraph::externalCompletionAt(const usize index)const{
    NWB_ASSERT(index < m_externalCompletions.size());
    const GpuExternalCompletionNode& completion = m_externalCompletions[index];
    return GpuTaskGraphExternalCompletionView{
        .id = GpuExternalCompletionId{ static_cast<u32>(index), m_generation },
        .identity = completion.identity,
        .markerLabel = markerLabel(completion.markerLabelOffset, completion.markerLabelSize),
        .token = completion.token,
        .hasToken = completion.hasToken,
    };
}

const QueueSubmissionToken* GpuTaskGraph::externalCompletionToken(
    const GpuExternalCompletionId& completion
)const noexcept{
    if(!validExternalCompletion(completion) || !m_externalCompletions[completion.index].hasToken)
        return nullptr;
    return &m_externalCompletions[completion.index].token;
}

Texture* GpuTaskGraph::textureForResource(const GpuGraphResourceId& resource)const noexcept{
    if(!validResource(resource))
        return nullptr;
    const GpuGraphResourceNode& node = m_resources[resource.index];
    return node.type == GpuGraphResourceType::Texture ? node.texture.get() : nullptr;
}

Buffer* GpuTaskGraph::bufferForResource(const GpuGraphResourceId& resource)const noexcept{
    if(!validResource(resource))
        return nullptr;
    const GpuGraphResourceNode& node = m_resources[resource.index];
    return node.type == GpuGraphResourceType::Buffer ? node.buffer.get() : nullptr;
}

RayTracingAccelStruct* GpuTaskGraph::accelStructForResource(const GpuGraphResourceId& resource)const noexcept{
    if(!validResource(resource))
        return nullptr;
    const GpuGraphResourceNode& node = m_resources[resource.index];
    return node.type == GpuGraphResourceType::AccelStruct ? node.accelStruct.get() : nullptr;
}

const void* GpuTaskGraph::uploadBlobData(const GpuUploadBlobId& blob, usize& outByteSize)const noexcept{
    outByteSize = 0u;
    const GpuUploadBlobNode* const node = findUploadBlob(blob);
    if(!node || node->bytes.empty())
        return nullptr;
    outByteSize = node->bytes.size();
    return node->bytes.data();
}

GraphicsPipeline* GpuTaskGraph::graphicsPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept{
    if(!validPipeline(pipeline))
        return nullptr;
    const GpuGraphPipelineNode& node = m_pipelines[pipeline.index];
    return node.type == GpuGraphPipelineType::Graphics ? node.graphicsPipeline.get() : nullptr;
}

ComputePipeline* GpuTaskGraph::computePipelineFor(const GpuGraphPipelineId& pipeline)const noexcept{
    if(!validPipeline(pipeline))
        return nullptr;
    const GpuGraphPipelineNode& node = m_pipelines[pipeline.index];
    return node.type == GpuGraphPipelineType::Compute ? node.computePipeline.get() : nullptr;
}

MeshletPipeline* GpuTaskGraph::meshletPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept{
    if(!validPipeline(pipeline))
        return nullptr;
    const GpuGraphPipelineNode& node = m_pipelines[pipeline.index];
    return node.type == GpuGraphPipelineType::Meshlet ? node.meshletPipeline.get() : nullptr;
}

RayTracingPipeline* GpuTaskGraph::rayTracingPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept{
    if(!validPipeline(pipeline))
        return nullptr;
    const GpuGraphPipelineNode& node = m_pipelines[pipeline.index];
    return node.type == GpuGraphPipelineType::RayTracing ? node.rayTracingPipeline.get() : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

