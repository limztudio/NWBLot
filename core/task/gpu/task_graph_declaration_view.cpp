// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiler.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u64 GpuTaskGraphDeclarationReadView::generation()const noexcept{
    return m_graph ? m_graph->m_generation : 0u;
}

u64 GpuTaskGraphDeclarationReadView::declarationRevision()const noexcept{
    return m_graph ? m_graph->m_declarationRevision : 0u;
}

bool GpuTaskGraphDeclarationReadView::validForDeviceGeneration(const u16 deviceGeneration)const noexcept{
    return m_graph && m_graph->validForDeviceGeneration(deviceGeneration);
}

bool GpuTaskGraphDeclarationReadView::validTask(const GpuTaskId& id)const noexcept{
    return m_graph && m_graph->validTask(id);
}

bool GpuTaskGraphDeclarationReadView::validResource(const GpuGraphResourceId& id)const noexcept{
    return m_graph && m_graph->validResource(id);
}

bool GpuTaskGraphDeclarationReadView::validResourceVersion(const GpuGraphResourceVersionId& id)const noexcept{
    return m_graph && m_graph->validResourceVersion(id);
}

bool GpuTaskGraphDeclarationReadView::validResourceSet(const GpuGraphResourceSetId& id)const noexcept{
    return m_graph && m_graph->validResourceSet(id);
}

bool GpuTaskGraphDeclarationReadView::validUploadBlob(const GpuUploadBlobId& id)const noexcept{
    return m_graph && m_graph->validUploadBlob(id);
}

bool GpuTaskGraphDeclarationReadView::validPipeline(const GpuGraphPipelineId& id)const noexcept{
    return m_graph && m_graph->validPipeline(id);
}

bool GpuTaskGraphDeclarationReadView::validExternalCompletion(const GpuExternalCompletionId& id)const noexcept{
    return m_graph && m_graph->validExternalCompletion(id);
}

usize GpuTaskGraphDeclarationReadView::taskCount()const noexcept{
    return m_graph ? m_graph->m_tasks.size() : 0u;
}

usize GpuTaskGraphDeclarationReadView::resourceCount()const noexcept{
    return m_graph ? m_graph->m_resources.size() : 0u;
}

usize GpuTaskGraphDeclarationReadView::resourceVersionCount()const noexcept{
    return m_graph ? m_graph->m_resourceVersions.size() : 0u;
}

usize GpuTaskGraphDeclarationReadView::resourceSetCount()const noexcept{
    return m_graph ? m_graph->m_resourceSets.size() : 0u;
}

usize GpuTaskGraphDeclarationReadView::uploadBlobCount()const noexcept{
    return m_graph ? m_graph->m_uploadBlobs.size() : 0u;
}

usize GpuTaskGraphDeclarationReadView::pipelineCount()const noexcept{
    return m_graph ? m_graph->m_pipelines.size() : 0u;
}

usize GpuTaskGraphDeclarationReadView::externalCompletionCount()const noexcept{
    return m_graph ? m_graph->m_externalCompletions.size() : 0u;
}

GpuTaskGraphTaskView GpuTaskGraphDeclarationReadView::taskAt(const usize index)const & noexcept{
    return m_graph && index < m_graph->m_tasks.size() ? m_graph->taskAt(index) : GpuTaskGraphTaskView{};
}

GpuTaskGraphResourceView GpuTaskGraphDeclarationReadView::resourceAt(const usize index)const & noexcept{
    return m_graph && index < m_graph->m_resources.size() ? m_graph->resourceAt(index) : GpuTaskGraphResourceView{};
}

GpuTaskGraphResourceVersionView GpuTaskGraphDeclarationReadView::resourceVersionAt(const usize index)const noexcept{
    return m_graph && index < m_graph->m_resourceVersions.size()
        ? m_graph->resourceVersionAt(index)
        : GpuTaskGraphResourceVersionView{}
    ;
}

GpuTaskGraphResourceSetView GpuTaskGraphDeclarationReadView::resourceSetAt(const usize index)const & noexcept{
    return m_graph && index < m_graph->m_resourceSets.size()
        ? m_graph->resourceSetAt(index)
        : GpuTaskGraphResourceSetView{}
    ;
}

GpuTaskGraphPipelineView GpuTaskGraphDeclarationReadView::pipelineAt(const usize index)const & noexcept{
    return m_graph && index < m_graph->m_pipelines.size() ? m_graph->pipelineAt(index) : GpuTaskGraphPipelineView{};
}

GpuTaskGraphExternalCompletionView GpuTaskGraphDeclarationReadView::externalCompletionAt(const usize index)const & noexcept{
    return m_graph && index < m_graph->m_externalCompletions.size()
        ? m_graph->externalCompletionAt(index)
        : GpuTaskGraphExternalCompletionView{}
    ;
}

const QueueSubmissionToken* GpuTaskGraphDeclarationReadView::externalCompletionToken(
    const GpuExternalCompletionId& completion
)const & noexcept{
    return m_graph ? m_graph->externalCompletionToken(completion) : nullptr;
}

Texture* GpuTaskGraphDeclarationReadView::textureForResource(const GpuGraphResourceId& resource)const & noexcept{
    return m_graph ? m_graph->textureForResource(resource) : nullptr;
}

Buffer* GpuTaskGraphDeclarationReadView::bufferForResource(const GpuGraphResourceId& resource)const & noexcept{
    return m_graph ? m_graph->bufferForResource(resource) : nullptr;
}

RayTracingAccelStruct* GpuTaskGraphDeclarationReadView::accelStructForResource(
    const GpuGraphResourceId& resource
)const & noexcept{
    return m_graph ? m_graph->accelStructForResource(resource) : nullptr;
}

const void* GpuTaskGraphDeclarationReadView::uploadBlobData(
    const GpuUploadBlobId& blob,
    usize& outByteSize
)const & noexcept{
    if(m_graph)
        return m_graph->uploadBlobData(blob, outByteSize);
    outByteSize = 0u;
    return nullptr;
}

GraphicsPipeline* GpuTaskGraphDeclarationReadView::graphicsPipelineFor(
    const GpuGraphPipelineId& pipeline
)const & noexcept{
    return m_graph ? m_graph->graphicsPipelineFor(pipeline) : nullptr;
}

ComputePipeline* GpuTaskGraphDeclarationReadView::computePipelineFor(const GpuGraphPipelineId& pipeline)const & noexcept{
    return m_graph ? m_graph->computePipelineFor(pipeline) : nullptr;
}

MeshletPipeline* GpuTaskGraphDeclarationReadView::meshletPipelineFor(const GpuGraphPipelineId& pipeline)const & noexcept{
    return m_graph ? m_graph->meshletPipelineFor(pipeline) : nullptr;
}

RayTracingPipeline* GpuTaskGraphDeclarationReadView::rayTracingPipelineFor(
    const GpuGraphPipelineId& pipeline
)const & noexcept{
    return m_graph ? m_graph->rayTracingPipelineFor(pipeline) : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

