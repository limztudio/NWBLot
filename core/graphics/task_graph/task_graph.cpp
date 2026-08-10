// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include "compiler.h"

#include <core/graphics/capture/command_ir.h>
#include <core/graphics/backend_selection.h>
#include <core/telemetry/frame_graph_contributor.h>

#include <global/atomic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextGeneration{ 1u };

[[nodiscard]] static u64 AllocateGeneration()noexcept{
    u64 generation = s_NextGeneration.fetch_add(1u, MemoryOrder::relaxed);
    if(generation == 0u)
        generation = s_NextGeneration.fetch_add(1u, MemoryOrder::relaxed);
    return generation;
}

// Keep primitive copies in the graph layer rather than duplicating a task payload in each renderer subsystem.
// The handles retain the typed imports until late native recording, while resource-state transitions remain
// compiler-owned through the helper-generated task uses below.
struct CopyTextureTask{
    struct Copy{
        GpuGraphResourceId sourceResource;
        TextureHandle source;
        TextureSlice sourceSlice;
        GpuGraphResourceId destinationResource;
        TextureHandle destination;
        TextureSlice destinationSlice;
    };

    struct Payload{
        explicit Payload(GraphicsArena& arena)
            : copies(arena)
        {}

        GraphicsVector<Copy> copies;
        QueueSubmissionToken* acceptedToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(payload.copies.empty())
            return false;

        for(const Copy& copy : payload.copies){
            if(!copy.source || !copy.destination)
                return false;
            if(
                context.commandIrCapture
                && !context.commandIrCapture->captureCopyTexture(
                    context.task,
                    context.packet,
                    context.queue,
                    copy.sourceResource,
                    copy.sourceSlice,
                    copy.destinationResource,
                    copy.destinationSlice
                )
            )
                return false;
            commandList.copyTexture(
                copy.destination.get(),
                copy.destinationSlice,
                copy.source.get(),
                copy.sourceSlice
            );
        }
        return true;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }
};

struct CopyBufferTask{
    struct Copy{
        GpuGraphResourceId sourceResource;
        BufferHandle source;
        u64 sourceOffsetBytes = 0u;
        GpuGraphResourceId destinationResource;
        BufferHandle destination;
        u64 destinationOffsetBytes = 0u;
        u64 dataSizeBytes = 0u;
    };

    struct Payload{
        explicit Payload(GraphicsArena& arena)
            : copies(arena)
        {}

        GraphicsVector<Copy> copies;
        QueueSubmissionToken* acceptedToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(payload.copies.empty())
            return false;

        for(const Copy& copy : payload.copies){
            if(!copy.source || !copy.destination || copy.dataSizeBytes == 0u)
                return false;
            if(
                context.commandIrCapture
                && !context.commandIrCapture->captureCopyBuffer(
                    context.task,
                    context.packet,
                    context.queue,
                    copy.sourceResource,
                    copy.sourceOffsetBytes,
                    copy.destinationResource,
                    copy.destinationOffsetBytes,
                    copy.dataSizeBytes
                )
            )
                return false;
            commandList.copyBuffer(
                copy.destination.get(),
                copy.destinationOffsetBytes,
                copy.source.get(),
                copy.sourceOffsetBytes,
                copy.dataSizeBytes
            );
        }
        return true;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }
};

struct ClearBufferTask{
    struct Payload{
        GpuGraphResourceId destinationResource;
        BufferHandle destination;
        u32 clearValue = 0u;
        QueueSubmissionToken* acceptedToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        if(!payload.destination)
            return false;
        if(
            context.commandIrCapture
            && !context.commandIrCapture->captureClearBuffer(
                context.task,
                context.packet,
                context.queue,
                payload.destinationResource,
                payload.clearValue
            )
        )
            return false;
        commandList.clearBufferUInt(payload.destination.get(), payload.clearValue);
        return true;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }
};

struct ClearTextureTask{
    struct Payload{
        GpuGraphResourceId destinationResource;
        TextureHandle destination;
        GpuClearTextureTaskDesc clearDesc;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        if(!payload.destination || payload.clearDesc.valueType >= GpuClearTextureTaskValueType::kCount)
            return false;
        if(
            context.commandIrCapture
            && !context.commandIrCapture->captureClearTexture(
                context.task,
                context.packet,
                context.queue,
                payload.destinationResource,
                payload.clearDesc
            )
        )
            return false;

        switch(payload.clearDesc.valueType){
        case GpuClearTextureTaskValueType::Float:
            commandList.clearTextureFloat(
                payload.destination.get(),
                payload.clearDesc.subresources,
                payload.clearDesc.floatValue
            );
            return true;
        case GpuClearTextureTaskValueType::UInt:
            commandList.clearTextureUInt(
                payload.destination.get(),
                payload.clearDesc.subresources,
                payload.clearDesc.uintValue
            );
            return true;
        case GpuClearTextureTaskValueType::Int:
            commandList.clearTextureInt(
                payload.destination.get(),
                payload.clearDesc.subresources,
                payload.clearDesc.intValue
            );
            return true;
        case GpuClearTextureTaskValueType::DepthStencil:
            commandList.clearDepthStencilTexture(
                payload.destination.get(),
                payload.clearDesc.subresources,
                payload.clearDesc.clearDepth,
                payload.clearDesc.depthValue,
                payload.clearDesc.clearStencil,
                payload.clearDesc.stencilValue
            );
            return true;
        default:
            return false;
        }
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.clearDesc.acceptedToken)
            *payload.clearDesc.acceptedToken = token;
    }
};

[[nodiscard]] static bool CompatibleResourceMetadata(
    const GpuTaskGraphResourceView& resource,
    const GpuGraphResourceDesc& desc
)noexcept{
    return resource.identity == desc.identity
        && resource.type == desc.type
        && resource.initialState == desc.initialState
        && resource.queueSharing == desc.queueSharing;
}

[[nodiscard]] static bool CompatiblePipelineMetadata(
    const GpuTaskGraphPipelineView& pipeline,
    const GpuGraphPipelineDesc& desc
)noexcept{
    // Identity and concrete pipeline kind define the graph-side table key.  Marker text is observational metadata,
    // matching resource imports where a later compatible import reuses the original graph-owned label.
    return pipeline.identity == desc.identity && pipeline.type == desc.type;
}

[[nodiscard]] static bool ClearTextureValueMatchesFormat(
    const TextureDesc& textureDesc,
    const GpuClearTextureTaskDesc& clearDesc
)noexcept{
    const FormatInfo& formatInfo = GetFormatInfo(textureDesc.format);
    const bool depthStencilFormat = formatInfo.hasDepth || formatInfo.hasStencil;
    switch(clearDesc.valueType){
    case GpuClearTextureTaskValueType::Float:
        return !depthStencilFormat
            && (formatInfo.kind == FormatKind::Normalized || formatInfo.kind == FormatKind::Float)
        ;
    case GpuClearTextureTaskValueType::UInt:
        return !depthStencilFormat && formatInfo.kind == FormatKind::Integer && !formatInfo.isSigned;
    case GpuClearTextureTaskValueType::Int:
        return !depthStencilFormat && formatInfo.kind == FormatKind::Integer && formatInfo.isSigned;
    case GpuClearTextureTaskValueType::DepthStencil:
        return depthStencilFormat
            && (!clearDesc.clearDepth || formatInfo.hasDepth)
            && (!clearDesc.clearStencil || formatInfo.hasStencil)
        ;
    default:
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraph::GpuTaskGraph(GraphicsArena& arena)
    : m_arena(arena)
    , m_tasks(arena)
    , m_dependencies(arena)
    , m_externalDependencies(arena)
    , m_resourceUses(arena)
    , m_resources(arena)
    , m_pipelines(arena)
    , m_externalCompletions(arena)
    , m_markerText(arena)
    , m_generation(__hidden_gpu_task_graph::AllocateGeneration())
{}

GpuTaskGraph::~GpuTaskGraph(){
    destroyTaskPayloads();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskId GpuTaskGraph::addTask(const GpuTaskDesc& desc){
    return appendTask(desc, nullptr, nullptr, nullptr, nullptr, nullptr);
}

GpuTaskId GpuTaskGraph::addCopyBufferTask(const GpuTaskDesc& desc, const GpuCopyBufferTaskDesc& copyDesc){
    if(
        desc.resourceUses
        || desc.resourceUseCount != 0u
        || !copyDesc.regions
        || copyDesc.regionCount == 0u
        || copyDesc.regionCount > Limit<u32>::s_Max
        || copyDesc.regionCount > Limit<usize>::s_Max / 2u
        || (static_cast<u8>(desc.queue.requiredCapabilities) & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u
    )
        return {};

    using CopyTask = __hidden_gpu_task_graph::CopyBufferTask;
    CopyTask::Payload* const payload = NewArenaObject<CopyTask::Payload>(m_arena, m_arena);
    if(!payload)
        return {};
    payload->copies.reserve(copyDesc.regionCount);
    payload->acceptedToken = copyDesc.acceptedToken;

    GraphicsVector<GpuTaskResourceUse> resourceUses(m_arena);
    resourceUses.reserve(copyDesc.regionCount * 2u);
    const auto appendResourceUse = [&](const GpuGraphResourceId resource, const ResourceStates::Mask state, const GpuTaskResourceAccess::Enum access){
        for(const GpuTaskResourceUse& existing : resourceUses){
            if(existing.resource != resource)
                continue;
            // A primitive copy cannot safely read and write the same imported buffer inside one task. Keep that
            // sequencing explicit in separate tasks instead of silently weakening its graph declarations.
            return existing.requiredState == state && existing.access == access;
        }
        // Buffer ownership and recorded-state handoffs are still whole-buffer. Keep the graph declaration equally
        // conservative even though the native payload validates and records an exact byte range.
        resourceUses.push_back(GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = state,
            .access = access,
        });
        return true;
    };
    const auto validCopyRange = [](const BufferDesc& bufferDesc, const u64 offsetBytes, const u64 dataSizeBytes){
        return dataSizeBytes != 0u
            && offsetBytes <= bufferDesc.byteSize
            && dataSizeBytes <= bufferDesc.byteSize - offsetBytes
        ;
    };

    bool valid = true;
    for(usize regionIndex = 0u; regionIndex < copyDesc.regionCount && valid; ++regionIndex){
        const GpuCopyBufferTaskRegion& region = copyDesc.regions[regionIndex];
        if(!validResource(region.source) || !validResource(region.destination)){
            valid = false;
            break;
        }
        const GpuGraphResourceNode& sourceResource = m_resources[region.source.index];
        const GpuGraphResourceNode& destinationResource = m_resources[region.destination.index];
        valid = region.source != region.destination
            && sourceResource.type == GpuGraphResourceType::Buffer
            && destinationResource.type == GpuGraphResourceType::Buffer
            && sourceResource.buffer
            && destinationResource.buffer
            && validCopyRange(sourceResource.buffer->getDescription(), region.sourceOffsetBytes, region.dataSizeBytes)
            && validCopyRange(destinationResource.buffer->getDescription(), region.destinationOffsetBytes, region.dataSizeBytes)
            && appendResourceUse(region.source, ResourceStates::CopySource, GpuTaskResourceAccess::Read)
            && appendResourceUse(region.destination, ResourceStates::CopyDest, GpuTaskResourceAccess::Write)
        ;
        if(valid){
            payload->copies.push_back(CopyTask::Copy{
                .sourceResource = region.source,
                .source = sourceResource.buffer,
                .sourceOffsetBytes = region.sourceOffsetBytes,
                .destinationResource = region.destination,
                .destination = destinationResource.buffer,
                .destinationOffsetBytes = region.destinationOffsetBytes,
                .dataSizeBytes = region.dataSizeBytes,
            });
        }
    }
    if(!valid){
        DestroyArenaObject(m_arena, payload);
        return {};
    }

    GpuTaskDesc resolvedDesc = desc;
    resolvedDesc.setResourceUses(resourceUses.data(), resourceUses.size());
    const GpuTaskId task = appendTask(
        resolvedDesc,
        payload,
        &RecordPayload<CopyTask>,
        &AcceptPayload<CopyTask>,
        nullptr,
        &DestroyPayload<CopyTask::Payload>
    );
    if(!task.valid())
        DestroyArenaObject(m_arena, payload);
    return task;
}

GpuTaskId GpuTaskGraph::addCopyTextureTask(const GpuTaskDesc& desc, const GpuCopyTextureTaskDesc& copyDesc){
    if(
        desc.resourceUses
        || desc.resourceUseCount != 0u
        || !copyDesc.regions
        || copyDesc.regionCount == 0u
        || copyDesc.regionCount > Limit<u32>::s_Max
        || copyDesc.regionCount > Limit<usize>::s_Max / 2u
        || (static_cast<u8>(desc.queue.requiredCapabilities) & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u
    )
        return {};

    using CopyTask = __hidden_gpu_task_graph::CopyTextureTask;
    CopyTask::Payload* const payload = NewArenaObject<CopyTask::Payload>(m_arena, m_arena);
    if(!payload)
        return {};
    payload->copies.reserve(copyDesc.regionCount);
    payload->acceptedToken = copyDesc.acceptedToken;

    GraphicsVector<GpuTaskResourceUse> resourceUses(m_arena);
    resourceUses.reserve(copyDesc.regionCount * 2u);
    const auto appendResourceUse = [&](const GpuGraphResourceId resource, const ResourceStates::Mask state, const GpuTaskResourceAccess::Enum access){
        for(const GpuTaskResourceUse& existing : resourceUses){
            if(existing.resource != resource)
                continue;
            // A primitive copy cannot safely read and write the same imported image inside one task. Keep that
            // sequencing explicit in separate tasks instead of silently weakening its graph declarations.
            return existing.requiredState == state && existing.access == access;
        }
        resourceUses.push_back(GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = state,
            .access = access,
        });
        return true;
    };

    bool valid = true;
    for(usize regionIndex = 0u; regionIndex < copyDesc.regionCount && valid; ++regionIndex){
        const GpuCopyTextureTaskRegion& region = copyDesc.regions[regionIndex];
        if(!validResource(region.source) || !validResource(region.destination)){
            valid = false;
            break;
        }
        const GpuGraphResourceNode& sourceResource = m_resources[region.source.index];
        const GpuGraphResourceNode& destinationResource = m_resources[region.destination.index];
        valid = region.source != region.destination
            && sourceResource.type == GpuGraphResourceType::Texture
            && destinationResource.type == GpuGraphResourceType::Texture
            && sourceResource.texture
            && destinationResource.texture
            && appendResourceUse(region.source, ResourceStates::CopySource, GpuTaskResourceAccess::Read)
            && appendResourceUse(region.destination, ResourceStates::CopyDest, GpuTaskResourceAccess::Write)
        ;
        if(valid){
            payload->copies.push_back(CopyTask::Copy{
                .sourceResource = region.source,
                .source = sourceResource.texture,
                .sourceSlice = region.sourceSlice,
                .destinationResource = region.destination,
                .destination = destinationResource.texture,
                .destinationSlice = region.destinationSlice,
            });
        }
    }
    if(!valid){
        DestroyArenaObject(m_arena, payload);
        return {};
    }

    GpuTaskDesc resolvedDesc = desc;
    resolvedDesc.setResourceUses(resourceUses.data(), resourceUses.size());
    const GpuTaskId task = appendTask(
        resolvedDesc,
        payload,
        &RecordPayload<CopyTask>,
        &AcceptPayload<CopyTask>,
        nullptr,
        &DestroyPayload<CopyTask::Payload>
    );
    if(!task.valid())
        DestroyArenaObject(m_arena, payload);
    return task;
}

GpuTaskId GpuTaskGraph::addClearBufferTask(const GpuTaskDesc& desc, const GpuClearBufferTaskDesc& clearDesc){
    if(
        desc.resourceUses
        || desc.resourceUseCount != 0u
        || !validResource(clearDesc.destination)
        || (static_cast<u8>(desc.queue.requiredCapabilities) & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u
    )
        return {};

    const GpuGraphResourceNode& destinationResource = m_resources[clearDesc.destination.index];
    if(
        destinationResource.type != GpuGraphResourceType::Buffer
        || !destinationResource.buffer
        || destinationResource.buffer->getDescription().byteSize == 0u
        || (destinationResource.buffer->getDescription().byteSize & (sizeof(u32) - 1u)) != 0u
    )
        return {};

    using ClearTask = __hidden_gpu_task_graph::ClearBufferTask;
    ClearTask::Payload* const payload = NewArenaObject<ClearTask::Payload>(m_arena);
    if(!payload)
        return {};
    payload->destinationResource = clearDesc.destination;
    payload->destination = destinationResource.buffer;
    payload->clearValue = clearDesc.clearValue;
    payload->acceptedToken = clearDesc.acceptedToken;

    const GpuTaskResourceUse resourceUse{
        .resource = clearDesc.destination,
        .range = {},
        .requiredState = ResourceStates::CopyDest,
        .access = GpuTaskResourceAccess::Write,
    };
    GpuTaskDesc resolvedDesc = desc;
    resolvedDesc.setResourceUses(&resourceUse, 1u);
    const GpuTaskId task = appendTask(
        resolvedDesc,
        payload,
        &RecordPayload<ClearTask>,
        &AcceptPayload<ClearTask>,
        nullptr,
        &DestroyPayload<ClearTask::Payload>
    );
    if(!task.valid())
        DestroyArenaObject(m_arena, payload);
    return task;
}

GpuTaskId GpuTaskGraph::addClearTextureTask(const GpuTaskDesc& desc, const GpuClearTextureTaskDesc& clearDesc){
    if(
        desc.resourceUses
        || desc.resourceUseCount != 0u
        || !validResource(clearDesc.destination)
        || clearDesc.valueType >= GpuClearTextureTaskValueType::kCount
        || (
            clearDesc.valueType == GpuClearTextureTaskValueType::DepthStencil
            && !clearDesc.clearDepth
            && !clearDesc.clearStencil
        )
        || (static_cast<u8>(desc.queue.requiredCapabilities) & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u
    )
        return {};

    const GpuGraphResourceNode& destinationResource = m_resources[clearDesc.destination.index];
    if(
        destinationResource.type != GpuGraphResourceType::Texture
        || !destinationResource.texture
        // vkCmdClear*Image cannot operate on multisampled images outside a render pass. This primitive helper has no
        // framebuffer/render-pass lowering, so preserve its transfer-only contract by rejecting MSAA up front.
        || destinationResource.texture->getDescription().sampleCount != 1u
    )
        return {};
    if(!__hidden_gpu_task_graph::ClearTextureValueMatchesFormat(
        destinationResource.texture->getDescription(),
        clearDesc
    ))
        return {};
    const TextureSubresourceSet resolvedSubresources = clearDesc.subresources.resolve(
        destinationResource.texture->getDescription(),
        TextureSubresourceMipResolve::Range
    );
    if(resolvedSubresources.numMipLevels == 0u || resolvedSubresources.numArraySlices == 0u)
        return {};

    using ClearTask = __hidden_gpu_task_graph::ClearTextureTask;
    ClearTask::Payload* const payload = NewArenaObject<ClearTask::Payload>(m_arena);
    if(!payload)
        return {};
    payload->destinationResource = clearDesc.destination;
    payload->destination = destinationResource.texture;
    payload->clearDesc = clearDesc;
    // Capture and native lowering retain the same exact graph-declared range rather than an all-subresources alias
    // that would need the original TextureDesc to resolve during a later validation/replay phase.
    payload->clearDesc.subresources = resolvedSubresources;

    const GpuTaskResourceUse resourceUse{
        .resource = clearDesc.destination,
        .range = GpuTaskResourceRange{
            .textureSubresources = resolvedSubresources,
        },
        .requiredState = ResourceStates::CopyDest,
        .access = GpuTaskResourceAccess::Write,
    };
    GpuTaskDesc resolvedDesc = desc;
    resolvedDesc.setResourceUses(&resourceUse, 1u);
    const GpuTaskId task = appendTask(
        resolvedDesc,
        payload,
        &RecordPayload<ClearTask>,
        &AcceptPayload<ClearTask>,
        nullptr,
        &DestroyPayload<ClearTask::Payload>
    );
    if(!task.valid())
        DestroyArenaObject(m_arena, payload);
    return task;
}

GpuGraphResourceId GpuTaskGraph::importResource(const GpuGraphResourceDesc& desc){
    if(!desc.identity || desc.markerLabel.empty() || desc.type >= GpuGraphResourceType::kCount)
        return {};

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuTaskGraphResourceView existing = resourceAt(resourceIndex);
        if(existing.identity != desc.identity)
            continue;
        if(!__hidden_gpu_task_graph::CompatibleResourceMetadata(existing, desc))
            return {};
        return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
    }

    return appendResource(desc);
}

GpuGraphResourceId GpuTaskGraph::importTexture(const TextureHandle& texture, const GpuGraphResourceDesc& desc){
    if(!texture || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphResourceType::Texture)
        return {};

    GpuGraphResourceDesc resolvedDesc = desc;
    if(resolvedDesc.initialState == ResourceStates::Unknown)
        resolvedDesc.initialState = texture->getDescription().initialState;
    if(resolvedDesc.queueSharing == ResourceQueueSharing::Exclusive)
        resolvedDesc.queueSharing = texture->getDescription().queueSharing;

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuGraphResourceNode& existing = m_resources[resourceIndex];
        if(existing.type == GpuGraphResourceType::Texture && existing.texture.get() == texture.get()){
            if(!__hidden_gpu_task_graph::CompatibleResourceMetadata(resourceAt(resourceIndex), resolvedDesc))
                return {};
            return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
        }
        if(existing.identity == resolvedDesc.identity)
            return {};
    }

    const GpuGraphResourceId resource = appendResource(resolvedDesc);
    if(resource.valid())
        m_resources[resource.index].texture = texture;
    return resource;
}

GpuGraphResourceId GpuTaskGraph::importBuffer(const BufferHandle& buffer, const GpuGraphResourceDesc& desc){
    if(!buffer || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphResourceType::Buffer)
        return {};

    GpuGraphResourceDesc resolvedDesc = desc;
    if(resolvedDesc.initialState == ResourceStates::Unknown)
        resolvedDesc.initialState = buffer->getDescription().initialState;
    if(resolvedDesc.queueSharing == ResourceQueueSharing::Exclusive)
        resolvedDesc.queueSharing = buffer->getDescription().queueSharing;

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuGraphResourceNode& existing = m_resources[resourceIndex];
        if(existing.type == GpuGraphResourceType::Buffer && existing.buffer.get() == buffer.get()){
            if(!__hidden_gpu_task_graph::CompatibleResourceMetadata(resourceAt(resourceIndex), resolvedDesc))
                return {};
            return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
        }
        if(existing.identity == resolvedDesc.identity)
            return {};
    }

    const GpuGraphResourceId resource = appendResource(resolvedDesc);
    if(resource.valid())
        m_resources[resource.index].buffer = buffer;
    return resource;
}

GpuGraphResourceId GpuTaskGraph::importAccelStruct(
    const RayTracingAccelStructHandle& accelStruct,
    const GpuGraphResourceDesc& desc
){
    if(!accelStruct || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphResourceType::AccelStruct)
        return {};

    GpuGraphResourceDesc resolvedDesc = desc;
    if(resolvedDesc.queueSharing == ResourceQueueSharing::Exclusive)
        resolvedDesc.queueSharing = accelStruct->getDescription().queueSharing;

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuGraphResourceNode& existing = m_resources[resourceIndex];
        if(existing.type == GpuGraphResourceType::AccelStruct && existing.accelStruct.get() == accelStruct.get()){
            if(!__hidden_gpu_task_graph::CompatibleResourceMetadata(resourceAt(resourceIndex), resolvedDesc))
                return {};
            return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
        }
        if(existing.identity == resolvedDesc.identity)
            return {};
    }

    const GpuGraphResourceId resource = appendResource(resolvedDesc);
    if(resource.valid())
        m_resources[resource.index].accelStruct = accelStruct;
    return resource;
}

GpuGraphResourceId GpuTaskGraph::importHazardDomain(const GpuGraphResourceDesc& desc){
    if(desc.type != GpuGraphResourceType::HazardDomain)
        return {};
    return importResource(desc);
}

GpuGraphPipelineId GpuTaskGraph::importPipeline(const GpuGraphPipelineDesc& desc){
    if(!desc.identity || desc.markerLabel.empty() || desc.type >= GpuGraphPipelineType::kCount)
        return {};

    for(usize pipelineIndex = 0u; pipelineIndex < m_pipelines.size(); ++pipelineIndex){
        const GpuTaskGraphPipelineView existing = pipelineAt(pipelineIndex);
        if(existing.identity != desc.identity)
            continue;
        if(!__hidden_gpu_task_graph::CompatiblePipelineMetadata(existing, desc))
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
            if(!__hidden_gpu_task_graph::CompatiblePipelineMetadata(pipelineAt(pipelineIndex), desc))
                return {};
            return GpuGraphPipelineId{ static_cast<u32>(pipelineIndex), m_generation };
        }
        if(existing.identity == desc.identity)
            return {};
    }

    const GpuGraphPipelineId id = appendPipeline(desc);
    if(id.valid())
        m_pipelines[id.index].graphicsPipeline = pipeline;
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
            if(!__hidden_gpu_task_graph::CompatiblePipelineMetadata(pipelineAt(pipelineIndex), desc))
                return {};
            return GpuGraphPipelineId{ static_cast<u32>(pipelineIndex), m_generation };
        }
        if(existing.identity == desc.identity)
            return {};
    }

    const GpuGraphPipelineId id = appendPipeline(desc);
    if(id.valid())
        m_pipelines[id.index].computePipeline = pipeline;
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
            if(!__hidden_gpu_task_graph::CompatiblePipelineMetadata(pipelineAt(pipelineIndex), desc))
                return {};
            return GpuGraphPipelineId{ static_cast<u32>(pipelineIndex), m_generation };
        }
        if(existing.identity == desc.identity)
            return {};
    }

    const GpuGraphPipelineId id = appendPipeline(desc);
    if(id.valid())
        m_pipelines[id.index].meshletPipeline = pipeline;
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
            if(!__hidden_gpu_task_graph::CompatiblePipelineMetadata(pipelineAt(pipelineIndex), desc))
                return {};
            return GpuGraphPipelineId{ static_cast<u32>(pipelineIndex), m_generation };
        }
        if(existing.identity == desc.identity)
            return {};
    }

    const GpuGraphPipelineId id = appendPipeline(desc);
    if(id.valid())
        m_pipelines[id.index].rayTracingPipeline = pipeline;
    return id;
}

GpuExternalCompletionId GpuTaskGraph::importExternalCompletion(const GpuExternalCompletionDesc& desc){
    if(!desc.identity || desc.markerLabel.empty())
        return {};

    for(usize completionIndex = 0u; completionIndex < m_externalCompletions.size(); ++completionIndex){
        if(m_externalCompletions[completionIndex].identity == desc.identity)
            return GpuExternalCompletionId{ static_cast<u32>(completionIndex), m_generation };
    }

    return appendExternalCompletion(desc);
}

void GpuTaskGraph::reset(){
    destroyTaskPayloads();
    m_tasks.clear();
    m_dependencies.clear();
    m_externalDependencies.clear();
    m_resourceUses.clear();
    m_resources.clear();
    m_pipelines.clear();
    m_externalCompletions.clear();
    m_markerText.clear();
    m_generation = __hidden_gpu_task_graph::AllocateGeneration();
}

bool GpuTaskGraph::validTask(const GpuTaskId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_tasks.size();
}

bool GpuTaskGraph::validResource(const GpuGraphResourceId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_resources.size();
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
        .dependencies = task.dependencyCount > 0u ? m_dependencies.data() + task.dependencyOffset : nullptr,
        .dependencyCount = task.dependencyCount,
        .externalDependencies = task.externalDependencyCount > 0u
            ? m_externalDependencies.data() + task.externalDependencyOffset
            : nullptr,
        .externalDependencyCount = task.externalDependencyCount,
        .resourceUses = task.resourceUseCount > 0u ? m_resourceUses.data() + task.resourceUseOffset : nullptr,
        .resourceUseCount = task.resourceUseCount,
        .hasPayload = task.payload != nullptr,
    };
}

GpuTaskGraphResourceView GpuTaskGraph::resourceAt(const usize index)const{
    NWB_ASSERT(index < m_resources.size());
    const GpuGraphResourceNode& resource = m_resources[index];
    return GpuTaskGraphResourceView{
        .id = GpuGraphResourceId{ static_cast<u32>(index), m_generation },
        .identity = resource.identity,
        .markerLabel = markerLabel(resource.markerLabelOffset, resource.markerLabelSize),
        .type = resource.type,
        .initialState = resource.initialState,
        .queueSharing = resource.queueSharing,
        .hasBackendResource = resource.texture != nullptr || resource.buffer != nullptr || resource.accelStruct != nullptr,
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
    };
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

bool GpuTaskGraph::recordTask(
    const GpuTaskId& taskID,
    CommandList& commandList,
    const GpuTaskRecordContext& context
)const{
    if(!validTask(taskID))
        return false;

    const GpuTaskNode& task = m_tasks[taskID.index];
    return task.payload && task.recordPayload && task.recordPayload(task.payload, commandList, context);
}

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
    case GpuCompiledBarrierType::TextureUav:
        if(resource.type != GpuGraphResourceType::Texture || !resource.texture)
            return false;
        commandList.setTextureState(
            resource.texture.get(),
            barrier.range.textureSubresources,
            barrier.after
        );
        return true;
    case GpuCompiledBarrierType::BufferTransition:
    case GpuCompiledBarrierType::BufferUav:
        if(resource.type != GpuGraphResourceType::Buffer || !resource.buffer)
            return false;
        commandList.setBufferState(resource.buffer.get(), barrier.after);
        return true;
    case GpuCompiledBarrierType::AccelStructTransition:
    case GpuCompiledBarrierType::AccelStructUav:
        if(resource.type != GpuGraphResourceType::AccelStruct || !resource.accelStruct)
            return false;
        commandList.setAccelStructState(resource.accelStruct.get(), barrier.after);
        return true;
    case GpuCompiledBarrierType::TextureOwnershipRelease:{
        const GpuPhysicalQueueInfo* const sourceQueue = compiledGraph.queueInfo(barrier.sourceQueue);
        const GpuPhysicalQueueInfo* const destinationQueue = compiledGraph.queueInfo(barrier.destinationQueue);
        if(
            resource.type != GpuGraphResourceType::Texture
            || !resource.texture
            || !resolveOwnershipQueues()
            || commandList.getDescription().queueType != sourceQueue->queueClass
        )
            return false;
        commandList.releaseTextureOwnership(
            resource.texture.get(),
            barrier.range.textureSubresources,
            destinationQueue->queueClass
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
            || commandList.getDescription().queueType != sourceQueue->queueClass
        )
            return false;
        commandList.releaseBufferOwnership(resource.buffer.get(), destinationQueue->queueClass);
        return true;
    }
    case GpuCompiledBarrierType::TextureOwnershipAcquire:
    case GpuCompiledBarrierType::BufferOwnershipAcquire:{
        const GpuPhysicalQueueInfo* const destinationQueue = compiledGraph.queueInfo(barrier.destinationQueue);
        if(!resolveOwnershipQueues() || commandList.getDescription().queueType != destinationQueue->queueClass)
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

void GpuTaskGraph::acceptTask(const GpuTaskId& taskID, const QueueSubmissionToken& token)noexcept{
    if(!validTask(taskID) || !token.valid())
        return;

    GpuTaskNode& task = m_tasks[taskID.index];
    if(task.payload && task.acceptPayload)
        task.acceptPayload(task.payload, token);
}

void GpuTaskGraph::discardTask(const GpuTaskId& taskID)noexcept{
    if(!validTask(taskID))
        return;

    GpuTaskNode& task = m_tasks[taskID.index];
    if(task.payload && task.discardPayload)
        task.discardPayload(task.payload);
}

bool GpuTaskGraph::appendFrameGraphTelemetry(
    Telemetry::FrameGraphBuilder& builder,
    const GpuTaskGraphAnalysis& analysis,
    Alloc::ScratchArena& scratchArena,
    const GpuTaskGraphTelemetryOptions& options
)const{
    if(
        !analysis.validFor(*this)
        || m_tasks.empty()
        || (options.legacyScheduleMismatchCount > 0u && !options.legacyScheduleMismatches)
        || (options.legacyQueueMismatchCount > 0u && !options.legacyQueueMismatches)
        || (options.queueAssignments && !options.queueAssignments->validFor(*this))
    )
        return false;
    for(usize mismatchIndex = 0u; mismatchIndex < options.legacyQueueMismatchCount; ++mismatchIndex){
        if(!validTask(options.legacyQueueMismatches[mismatchIndex]))
            return false;
    }
    if(options.queueAssignments){
        for(usize taskIndex = 0u; taskIndex < m_tasks.size(); ++taskIndex){
            if(!options.queueAssignments->find(taskAt(taskIndex).id))
                return false;
        }
    }

    Vector<Telemetry::FrameGraphNodeHandle, Alloc::ScratchArena> resourceNodes(scratchArena);
    Vector<Telemetry::FrameGraphNodeHandle, Alloc::ScratchArena> completionNodes(scratchArena);
    Vector<Telemetry::FrameGraphNodeHandle, Alloc::ScratchArena> taskNodes(scratchArena);
    resourceNodes.reserve(m_resources.size());
    completionNodes.reserve(m_externalCompletions.size());
    taskNodes.reserve(m_tasks.size());

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuTaskGraphResourceView resource = resourceAt(resourceIndex);
        resourceNodes.push_back(builder.addResource(resource.identity, resource.markerLabel));
    }
    for(usize completionIndex = 0u; completionIndex < m_externalCompletions.size(); ++completionIndex){
        const GpuTaskGraphExternalCompletionView completion = externalCompletionAt(completionIndex);
        completionNodes.push_back(builder.addExternal(completion.identity, completion.markerLabel));
    }
    for(usize taskIndex = 0u; taskIndex < m_tasks.size(); ++taskIndex){
        const GpuTaskGraphTaskView task = taskAt(taskIndex);
        u8 flags = GpuTaskGraphTelemetryNodeFlag::None;
        if(options.queueAssignments){
            const GpuTaskQueueAssignment* const assignment = options.queueAssignments->find(task.id);
            NWB_ASSERT(assignment);
            switch(assignment->queueClass){
            case CommandQueue::Graphics:
                flags |= GpuTaskGraphTelemetryNodeFlag::AssignedGraphicsQueue;
                break;
            case CommandQueue::Compute:
                flags |= GpuTaskGraphTelemetryNodeFlag::AssignedComputeQueue;
                break;
            case CommandQueue::Transfer:
                flags |= GpuTaskGraphTelemetryNodeFlag::AssignedTransferQueue;
                break;
            default:
                return false;
            }
            if(assignment->dedicated)
                flags |= GpuTaskGraphTelemetryNodeFlag::AssignedDedicatedQueue;
            if(assignment->reason == GpuTaskQueueAssignmentReason::Fallback)
                flags |= GpuTaskGraphTelemetryNodeFlag::QueueAssignmentFallback;
        }
        for(usize mismatchIndex = 0u; mismatchIndex < options.legacyQueueMismatchCount; ++mismatchIndex){
            if(options.legacyQueueMismatches[mismatchIndex] == task.id){
                flags |= GpuTaskGraphTelemetryNodeFlag::LegacyQueueAssignmentMismatch;
                break;
            }
        }
        taskNodes.push_back(builder.addPass(task.identity, task.markerLabel, flags));
    }

    for(usize taskIndex = 0u; taskIndex < m_tasks.size(); ++taskIndex){
        const GpuTaskGraphTaskView task = taskAt(taskIndex);
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            const Telemetry::FrameGraphNodeHandle resourceNode = resourceNodes[use.resource.index];
            const Telemetry::FrameGraphNodeHandle taskNode = taskNodes[taskIndex];
            if(use.access == GpuTaskResourceAccess::Read || use.access == GpuTaskResourceAccess::ReadWrite)
                builder.addEdge(resourceNode, taskNode, Telemetry::FrameGraphEdgeKind::Reads);
            if(use.access == GpuTaskResourceAccess::Write || use.access == GpuTaskResourceAccess::ReadWrite)
                builder.addEdge(taskNode, resourceNode, Telemetry::FrameGraphEdgeKind::Writes);
        }
    }
    for(const GpuTaskExternalDependencyEdge& edge : analysis.externalDependencies())
        builder.addEdge(completionNodes[edge.completion.index], taskNodes[edge.consumer.index], Telemetry::FrameGraphEdgeKind::DependsOn);
    for(const GpuTaskDependencyEdge& edge : analysis.edges()){
        u8 flags = GpuTaskGraphTelemetryEdgeFlag::None;
        if(analysis.hasExplicitEdge(edge.producer, edge.consumer))
            flags |= GpuTaskGraphTelemetryEdgeFlag::ExplicitDependency;
        if(analysis.hasInferredEdge(edge.producer, edge.consumer))
            flags |= GpuTaskGraphTelemetryEdgeFlag::InferredDependency;
        for(usize mismatchIndex = 0u; mismatchIndex < options.legacyScheduleMismatchCount; ++mismatchIndex){
            const GpuTaskDependencyEdge& mismatch = options.legacyScheduleMismatches[mismatchIndex];
            if(mismatch.producer == edge.producer && mismatch.consumer == edge.consumer){
                flags |= GpuTaskGraphTelemetryEdgeFlag::MissingLegacyScheduleDependency;
                break;
            }
        }
        builder.addEdge(
            taskNodes[edge.producer.index],
            taskNodes[edge.consumer.index],
            Telemetry::FrameGraphEdgeKind::DependsOn,
            flags
        );
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskId GpuTaskGraph::appendTask(
    const GpuTaskDesc& desc,
    void* const payload,
    const GpuTaskRecordThunk recordPayload,
    const GpuTaskAcceptedThunk acceptPayload,
    const GpuTaskDiscardedThunk discardPayload,
    const GpuTaskPayloadDestroyThunk destroyPayload
){
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || m_tasks.size() >= Limit<u32>::s_Max
        || desc.dependencyCount > Limit<u32>::s_Max - m_dependencies.size()
        || desc.externalDependencyCount > Limit<u32>::s_Max - m_externalDependencies.size()
        || desc.resourceUseCount > Limit<u32>::s_Max - m_resourceUses.size()
        || (desc.dependencyCount > 0u && !desc.dependencies)
        || (desc.externalDependencyCount > 0u && !desc.externalDependencies)
        || (desc.resourceUseCount > 0u && !desc.resourceUses)
    )
        return {};

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize))
        return {};

    GpuTaskNode task;
    task.identity = desc.identity;
    task.queue = desc.queue;
    task.scheduling = desc.scheduling;
    task.markerLabelOffset = markerLabelOffset;
    task.markerLabelSize = markerLabelSize;
    task.dependencyOffset = static_cast<u32>(m_dependencies.size());
    task.dependencyCount = static_cast<u32>(desc.dependencyCount);
    task.externalDependencyOffset = static_cast<u32>(m_externalDependencies.size());
    task.externalDependencyCount = static_cast<u32>(desc.externalDependencyCount);
    task.resourceUseOffset = static_cast<u32>(m_resourceUses.size());
    task.resourceUseCount = static_cast<u32>(desc.resourceUseCount);
    task.payload = payload;
    task.recordPayload = recordPayload;
    task.acceptPayload = acceptPayload;
    task.discardPayload = discardPayload;
    task.destroyPayload = destroyPayload;

    for(usize dependencyIndex = 0u; dependencyIndex < desc.dependencyCount; ++dependencyIndex)
        m_dependencies.push_back(desc.dependencies[dependencyIndex]);
    for(usize dependencyIndex = 0u; dependencyIndex < desc.externalDependencyCount; ++dependencyIndex)
        m_externalDependencies.push_back(desc.externalDependencies[dependencyIndex]);
    for(usize useIndex = 0u; useIndex < desc.resourceUseCount; ++useIndex)
        m_resourceUses.push_back(desc.resourceUses[useIndex]);

    const u32 index = static_cast<u32>(m_tasks.size());
    m_tasks.push_back(Move(task));
    return GpuTaskId{ index, m_generation };
}

GpuGraphResourceId GpuTaskGraph::appendResource(const GpuGraphResourceDesc& desc){
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || desc.type >= GpuGraphResourceType::kCount
        || m_resources.size() >= Limit<u32>::s_Max
    )
        return {};

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize))
        return {};

    GpuGraphResourceNode resource;
    resource.identity = desc.identity;
    resource.type = desc.type;
    resource.initialState = desc.initialState;
    resource.queueSharing = desc.queueSharing;
    resource.markerLabelOffset = markerLabelOffset;
    resource.markerLabelSize = markerLabelSize;

    const u32 index = static_cast<u32>(m_resources.size());
    m_resources.push_back(Move(resource));
    return GpuGraphResourceId{ index, m_generation };
}

GpuGraphPipelineId GpuTaskGraph::appendPipeline(const GpuGraphPipelineDesc& desc){
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || desc.type >= GpuGraphPipelineType::kCount
        || m_pipelines.size() >= Limit<u32>::s_Max
    )
        return {};

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize))
        return {};

    GpuGraphPipelineNode pipeline;
    pipeline.identity = desc.identity;
    pipeline.type = desc.type;
    pipeline.markerLabelOffset = markerLabelOffset;
    pipeline.markerLabelSize = markerLabelSize;

    const u32 index = static_cast<u32>(m_pipelines.size());
    m_pipelines.push_back(Move(pipeline));
    return GpuGraphPipelineId{ index, m_generation };
}

GpuExternalCompletionId GpuTaskGraph::appendExternalCompletion(const GpuExternalCompletionDesc& desc){
    if(!desc.identity || desc.markerLabel.empty() || m_externalCompletions.size() >= Limit<u32>::s_Max)
        return {};

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize))
        return {};

    GpuExternalCompletionNode completion;
    completion.identity = desc.identity;
    completion.markerLabelOffset = markerLabelOffset;
    completion.markerLabelSize = markerLabelSize;

    const u32 index = static_cast<u32>(m_externalCompletions.size());
    m_externalCompletions.push_back(Move(completion));
    return GpuExternalCompletionId{ index, m_generation };
}

bool GpuTaskGraph::appendMarkerLabel(const AStringView text, u32& outOffset, u32& outSize){
    if(
        text.empty()
        || !text.data()
        || text.size() > Limit<u32>::s_Max
        || text.size() > Limit<u32>::s_Max - m_markerText.size()
    )
        return false;

    outOffset = static_cast<u32>(m_markerText.size());
    outSize = static_cast<u32>(text.size());
    const usize nextSize = m_markerText.size() + text.size();
    m_markerText.resize(nextSize);
    NWB_MEMCPY(m_markerText.data() + outOffset, outSize, text.data(), text.size());
    return true;
}

AStringView GpuTaskGraph::markerLabel(const u32 offset, const u32 size)const{
    NWB_ASSERT(offset <= m_markerText.size());
    NWB_ASSERT(size <= m_markerText.size() - offset);
    return AStringView(reinterpret_cast<const char*>(m_markerText.data() + offset), size);
}

void GpuTaskGraph::destroyTaskPayloads()noexcept{
    for(GpuTaskNode& task : m_tasks){
        if(task.payload && task.destroyPayload)
            task.destroyPayload(m_arena, task.payload);
        task.payload = nullptr;
        task.destroyPayload = nullptr;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

