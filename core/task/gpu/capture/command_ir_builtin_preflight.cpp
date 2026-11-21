// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_builtin_preflight.h"

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/backend_selection/resource_validation.h>
#include <core/task/gpu/texture_clear_value.h>
#include <core/task/gpu/task_graph.h>
#include <core/graphics/backend_selection/texture_clear_contract.h>
#include <core/graphics/backend_selection/texture_copy_contract.h>

#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


namespace __hidden_gpu_command_ir_builtin_preflight{

[[nodiscard]] static b8 BufferRangeContains(const BufferDesc& description, const BufferRange& declaration, const u64 offsetBytes, const u64 sizeBytes)noexcept{
    if(!GraphicsBackend::IsBufferRangeInBounds(description, offsetBytes, sizeBytes) || declaration.byteOffset > description.byteSize)
        return false;

    const u64 declaredSize = declaration.byteSize == BufferRange::s_AllBytes ? description.byteSize - declaration.byteOffset : declaration.byteSize;
    if(declaredSize > description.byteSize - declaration.byteOffset || offsetBytes < declaration.byteOffset)
        return false;
    const u64 relativeOffset = offsetBytes - declaration.byteOffset;
    return relativeOffset <= declaredSize && sizeBytes <= declaredSize - relativeOffset;
}

[[nodiscard]] static b8 TextureSubresourcesAreCanonical(const TextureDesc& description, const TextureSubresourceSet& subresources)noexcept{
    if(subresources.numMipLevels == 0u || subresources.numArraySlices == 0u || subresources.baseMipLevel >= description.mipLevels)
        return false;
    return subresources == subresources.resolve(description, TextureSubresourceMipResolve::Range);
}

[[nodiscard]] static b8 TextureSubresourcesContain(const TextureSubresourceSet& declaration, const TextureDesc& description, const TextureSubresourceSet& requested)noexcept{
    if(!TextureSubresourcesAreCanonical(description, requested))
        return false;

    const TextureSubresourceSet resolvedDeclaration = declaration.resolve(description, TextureSubresourceMipResolve::Range);
    if(
        resolvedDeclaration.numMipLevels == 0u
        || resolvedDeclaration.numArraySlices == 0u
        || requested.baseMipLevel < resolvedDeclaration.baseMipLevel
        || requested.baseMipLevel - resolvedDeclaration.baseMipLevel >= resolvedDeclaration.numMipLevels
        || requested.numMipLevels > resolvedDeclaration.numMipLevels
            - (requested.baseMipLevel - resolvedDeclaration.baseMipLevel)
        || requested.baseArraySlice < resolvedDeclaration.baseArraySlice
        || requested.baseArraySlice - resolvedDeclaration.baseArraySlice >= resolvedDeclaration.numArraySlices
        || requested.numArraySlices > resolvedDeclaration.numArraySlices
            - (requested.baseArraySlice - resolvedDeclaration.baseArraySlice)
    )
        return false;
    return true;
}

[[nodiscard]] static b8 TextureSliceIsDeclared(const TextureSubresourceSet& declaration, const TextureDesc& description, const TextureSlice& resolvedSlice)noexcept{
    const TextureSubresourceSet requested(resolvedSlice.mipLevel, 1u, resolvedSlice.arraySlice, 1u);
    return TextureSubresourcesContain(declaration, description, requested);
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateBufferCopyImpl(
    const GpuGraphResourceId sourceId,
    const GpuGraphResourceId destinationId,
    const u64 sourceOffsetBytes,
    const u64 destinationOffsetBytes,
    const u64 dataSizeBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task
)noexcept{
    Buffer* const source = graph.bufferForResource(sourceId);
    Buffer* const destination = graph.bufferForResource(destinationId);
    if(!source || !destination){
        if(!graph.validResource(sourceId) || !graph.validResource(destinationId))
            return GpuCommandIrReplayError::InvalidResource;
        if(graph.resourceTypeAt(sourceId.index) != GpuGraphResourceType::Buffer || graph.resourceTypeAt(destinationId.index) != GpuGraphResourceType::Buffer)
            return GpuCommandIrReplayError::ResourceTypeMismatch;
        return GpuCommandIrReplayError::MissingBackendResource;
    }

    const BufferDesc& sourceDescription = source->getCreationDescription();
    const BufferDesc& destinationDescription = destination->getCreationDescription();
    b8 hasSourceUse = false;
    b8 hasDestinationUse = false;
    b8 hasSourceRange = false;
    b8 hasDestinationRange = false;
    for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
        const GpuTaskResourceUse& use = task.resourceUses[useIndex];
        if(use.resource == sourceId && use.requiredState == ResourceStates::CopySource && use.access == GpuTaskResourceAccess::Read){
            hasSourceUse = true;
            if(!hasSourceRange && BufferRangeContains(sourceDescription, use.range.bufferRange, sourceOffsetBytes, dataSizeBytes))
                hasSourceRange = true;
        }
        if(use.resource == destinationId && use.requiredState == ResourceStates::CopyDest && use.access == GpuTaskResourceAccess::Write){
            hasDestinationUse = true;
            if(!hasDestinationRange && BufferRangeContains(destinationDescription, use.range.bufferRange, destinationOffsetBytes, dataSizeBytes))
                hasDestinationRange = true;
        }
        if(hasSourceRange && hasDestinationRange)
            break;
    }
    if(!hasSourceUse || !hasDestinationUse)
        return GpuCommandIrReplayError::ResourceUseMismatch;
    if(
        dataSizeBytes == 0u
        || !hasSourceRange
        || !hasDestinationRange
        || (source == destination && UnsignedRangesOverlap(sourceOffsetBytes, dataSizeBytes, destinationOffsetBytes, dataSizeBytes))
    )
        return GpuCommandIrReplayError::InvalidBufferCopy;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateTextureCopyImpl(
    const GpuGraphResourceId sourceId,
    const GpuGraphResourceId destinationId,
    const TextureSlice& sourceSlice,
    const TextureSlice& destinationSlice,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& queue
)noexcept{
    if(sourceId == destinationId)
        return GpuCommandIrReplayError::InvalidResource;
    Texture* const source = graph.textureForResource(sourceId);
    Texture* const destination = graph.textureForResource(destinationId);
    if(!source || !destination){
        if(!graph.validResource(sourceId) || !graph.validResource(destinationId))
            return GpuCommandIrReplayError::InvalidResource;
        if(graph.resourceTypeAt(sourceId.index) != GpuGraphResourceType::Texture || graph.resourceTypeAt(destinationId.index) != GpuGraphResourceType::Texture)
            return GpuCommandIrReplayError::ResourceTypeMismatch;
        return GpuCommandIrReplayError::MissingBackendResource;
    }
    if(source == destination)
        return GpuCommandIrReplayError::InvalidTextureCopy;

    const TextureDesc& sourceDescription = source->getCreationDescription();
    const TextureDesc& destinationDescription = destination->getCreationDescription();
    const Expected<GraphicsBackend::TextureCopyContract> contract = GraphicsBackend::ResolveTextureCopyContract(sourceDescription, sourceSlice, destinationDescription, destinationSlice);
    const b8 hasContract = contract.has_value();

    b8 hasSourceUse = false;
    b8 hasDestinationUse = false;
    b8 hasSourceSlice = false;
    b8 hasDestinationSlice = false;
    for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
        const GpuTaskResourceUse& use = task.resourceUses[useIndex];
        if(use.resource == sourceId && use.requiredState == ResourceStates::CopySource && use.access == GpuTaskResourceAccess::Read){
            hasSourceUse = true;
            if(hasContract && !hasSourceSlice && TextureSliceIsDeclared(use.range.textureSubresources, sourceDescription, contract->sourceSlice))
                hasSourceSlice = true;
        }
        if(use.resource == destinationId && use.requiredState == ResourceStates::CopyDest && use.access == GpuTaskResourceAccess::Write){
            hasDestinationUse = true;
            if(hasContract && !hasDestinationSlice && TextureSliceIsDeclared(use.range.textureSubresources, destinationDescription, contract->destinationSlice))
                hasDestinationSlice = true;
        }
        if(hasSourceSlice && hasDestinationSlice)
            break;
    }
    if(!hasSourceUse || !hasDestinationUse)
        return GpuCommandIrReplayError::ResourceUseMismatch;
    if(!hasContract || !hasSourceSlice || !hasDestinationSlice)
        return GpuCommandIrReplayError::InvalidTextureCopy;

    const u8 taskCapabilities = static_cast<u8>(task.commands.allowedCapabilities());
    const u8 physicalQueueCapabilities = static_cast<u8>(queue.capabilities);
    const b8 taskAndQueueShareComputeOrGraphics = (taskCapabilities & physicalQueueCapabilities & static_cast<u8>(GpuQueueCapability::Compute | GpuQueueCapability::Graphics)) != 0u;
    const b8 taskHasGraphics = (taskCapabilities & static_cast<u8>(GpuQueueCapability::Graphics)) != 0u;
    const b8 queueHasGraphics = (physicalQueueCapabilities & static_cast<u8>(GpuQueueCapability::Graphics)) != 0u;
    if(
        (contract->queueRequirement == GraphicsBackend::TextureCopyQueueRequirement::ComputeOrGraphics && !taskAndQueueShareComputeOrGraphics)
        || (contract->queueRequirement == GraphicsBackend::TextureCopyQueueRequirement::Graphics && (!taskHasGraphics || !queueHasGraphics))
    )
        return GpuCommandIrReplayError::InvalidTextureCopy;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateBufferClearImpl(
    const GpuGraphResourceId destinationId,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task
)noexcept{
    Buffer* const destination = graph.bufferForResource(destinationId);
    if(!destination){
        if(!graph.validResource(destinationId))
            return GpuCommandIrReplayError::InvalidResource;
        if(graph.resourceTypeAt(destinationId.index) != GpuGraphResourceType::Buffer)
            return GpuCommandIrReplayError::ResourceTypeMismatch;
        return GpuCommandIrReplayError::MissingBackendResource;
    }

    const GpuTaskResourceUse* const destinationUse = GpuCommandIrDetail::FindTaskResourceUse(task, destinationId, ResourceStates::CopyDest, GpuTaskResourceAccess::Write);
    if(!destinationUse)
        return GpuCommandIrReplayError::ResourceUseMismatch;

    const BufferDesc& description = destination->getCreationDescription();
    if(description.byteSize == 0u || (description.byteSize & (sizeof(u32) - 1u)) != 0u || !destinationUse->range.bufferRange.isEntireBuffer(description))
        return GpuCommandIrReplayError::InvalidBufferClear;
    return GpuCommandIrReplayError::None;
}

struct TextureClearDestination{
    Texture& texture;
    const GpuTaskResourceUse& use;
};

[[nodiscard]] static Expected<TextureClearDestination, GpuCommandIrReplayError::Enum> ResolveTextureClearDestination(
    const GpuGraphResourceId destinationId,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task
)noexcept{
    Texture* const destination = graph.textureForResource(destinationId);
    if(!destination){
        if(!graph.validResource(destinationId))
            return MakeUnexpected(GpuCommandIrReplayError::InvalidResource);
        if(graph.resourceTypeAt(destinationId.index) != GpuGraphResourceType::Texture)
            return MakeUnexpected(GpuCommandIrReplayError::ResourceTypeMismatch);
        return MakeUnexpected(GpuCommandIrReplayError::MissingBackendResource);
    }

    const GpuTaskResourceUse* const destinationUse = GpuCommandIrDetail::FindTaskResourceUse(task, destinationId, ResourceStates::CopyDest, GpuTaskResourceAccess::Write);
    if(!destinationUse)
        return MakeUnexpected(GpuCommandIrReplayError::ResourceUseMismatch);
    return TextureClearDestination{ .texture = *destination, .use = *destinationUse, };
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateTextureClearImpl(
    const GpuGraphResourceId destinationId,
    const TextureSubresourceSet& destinationSubresources,
    const GpuClearTextureTaskValueType::Enum clearTextureValueType,
    const b8 clearDepth,
    const b8 clearStencil,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& queue
)noexcept{
    const auto resolvedDestination = ResolveTextureClearDestination(destinationId, graph, task);
    if(!resolvedDestination)
        return resolvedDestination.error();
    Texture& destination = resolvedDestination->texture;
    const GpuTaskResourceUse& destinationUse = resolvedDestination->use;

    const TextureDesc& description = destination.getCreationDescription();
    const auto valueKind = GpuTaskGraphClearDetail::TryMapTextureClearValueKind(clearTextureValueType);
    Expected<GraphicsBackend::TextureClearContract> clearContract = MakeUnexpected(Failure{});
    if(
        !TextureSubresourcesAreCanonical(description, destinationSubresources)
        || !TextureSubresourcesContain(destinationUse.range.textureSubresources, description, destinationSubresources)
        || !valueKind
        || !(clearContract = GraphicsBackend::ResolveTextureClearContract(description, destinationSubresources, *valueKind, clearDepth, clearStencil))
        || clearContract->subresources != destinationSubresources
        || !GraphicsBackend::TextureClearQueueRequirementSatisfied(clearContract->queueRequirement, task.commands.allowedCapabilities(), queue.capabilities)
    )
        return GpuCommandIrReplayError::InvalidTextureClear;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateTextureRectUIntClearImpl(
    const GpuGraphResourceId destinationId,
    const TextureSubresourceSet& destinationSubresources,
    const Rect& clearRect,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& queue
)noexcept{
    const auto resolvedDestination = ResolveTextureClearDestination(destinationId, graph, task);
    if(!resolvedDestination)
        return resolvedDestination.error();
    Texture& destination = resolvedDestination->texture;
    const GpuTaskResourceUse& destinationUse = resolvedDestination->use;

    const TextureDesc& description = destination.getCreationDescription();
    Expected<GraphicsBackend::TextureClearContract> clearContract = MakeUnexpected(Failure{});
    const Box clearBox(clearRect, 0, Limit<i32>::s_Max);
    if(
        clearRect.maxX <= clearRect.minX
        || clearRect.maxY <= clearRect.minY
        || description.sampleCount != 1u
        || !TextureSubresourcesAreCanonical(description, destinationSubresources)
        || !TextureSubresourcesContain(destinationUse.range.textureSubresources, description, destinationSubresources)
        || !(clearContract = GraphicsBackend::ResolveTextureClearContract(description, destinationSubresources, GraphicsBackend::TextureClearValueKind::UInt, false, false))
        || clearContract->subresources != destinationSubresources
        || !GraphicsBackend::TextureClearQueueRequirementSatisfied(GraphicsBackend::TextureClearBoxQueueRequirement(description, clearContract->subresources, clearBox), task.commands.allowedCapabilities(), queue.capabilities)
    )
        return GpuCommandIrReplayError::InvalidTextureClear;
    return GpuCommandIrReplayError::None;
}

};


GpuCommandIrReplayError::Enum GpuCommandIrBuiltinPreflight::Validate(
    const GpuCommandIrBuiltinTaskRecord& record,
    const BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& queue
)noexcept{
    using namespace __hidden_gpu_command_ir_builtin_preflight;
    switch(record.opcode){
    case GpuCommandIrOpcode::CopyBuffer:
        return ValidateBufferCopyImpl(record.source, record.destination, record.sourceOffsetBytes, record.destinationOffsetBytes, record.dataSizeBytes, graph, task);
    case GpuCommandIrOpcode::CopyTexture:
        return ValidateTextureCopyImpl(record.source, record.destination, record.sourceSlice, record.destinationSlice, graph, task, queue);
    case GpuCommandIrOpcode::ClearBuffer:
        return ValidateBufferClearImpl(record.destination, graph, task);
    case GpuCommandIrOpcode::ClearTexture:
        return ValidateTextureClearImpl(record.destination, record.destinationSubresources, record.clearTextureValueType, record.clearDepth, record.clearStencil, graph, task, queue);
    case GpuCommandIrOpcode::ClearTextureRectUInt:
        return ValidateTextureRectUIntClearImpl(record.destination, record.destinationSubresources, record.clearRect, graph, task, queue);
    case GpuCommandIrOpcode::UploadBuffer:
    case GpuCommandIrOpcode::UploadTexture:
        return GpuCommandIrDetail::ValidateUploadOperation(record, blobBytes, graph, task, queue);
    default:
        return GpuCommandIrReplayError::InvalidStream;
    }
}

GpuCommandIrReplayError::Enum GpuCommandIrBuiltinPreflight::ValidateWire(
    const GpuCommandIrWireOpcode::Enum opcode,
    const u8* const recordPtr,
    const u64 graphGeneration,
    const BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& queue
)noexcept{
    using namespace __hidden_gpu_command_ir_builtin_preflight;
    switch(opcode){
    case GpuCommandIrWireOpcode::CopyBuffer:{
        const auto wire = GpuCommandIrDetail::ReadWirePod<GpuCommandIrCopyBufferRecord>(recordPtr);
        return ValidateBufferCopyImpl(
            { .generation = graphGeneration, .index = wire.sourceResourceIndex },
            { .generation = graphGeneration, .index = wire.destinationResourceIndex },
            wire.sourceOffsetBytes,
            wire.destinationOffsetBytes,
            wire.dataSizeBytes,
            graph,
            task
        );
    }
    case GpuCommandIrWireOpcode::CopyTexture:{
        const auto wire = GpuCommandIrDetail::ReadWirePod<GpuCommandIrCopyTextureRecord>(recordPtr);
        const TextureSlice sourceSlice{ .x = wire.sourceSlice.x, .y = wire.sourceSlice.y, .z = wire.sourceSlice.z, .width = wire.sourceSlice.width, .height = wire.sourceSlice.height, .depth = wire.sourceSlice.depth, .mipLevel = wire.sourceSlice.mipLevel, .arraySlice = wire.sourceSlice.arraySlice };
        const TextureSlice destinationSlice{ .x = wire.destinationSlice.x, .y = wire.destinationSlice.y, .z = wire.destinationSlice.z, .width = wire.destinationSlice.width, .height = wire.destinationSlice.height, .depth = wire.destinationSlice.depth, .mipLevel = wire.destinationSlice.mipLevel, .arraySlice = wire.destinationSlice.arraySlice };
        return ValidateTextureCopyImpl(
            { .generation = graphGeneration, .index = wire.sourceResourceIndex },
            { .generation = graphGeneration, .index = wire.destinationResourceIndex },
            sourceSlice,
            destinationSlice,
            graph,
            task,
            queue
        );
    }
    case GpuCommandIrWireOpcode::ClearBuffer:{
        const auto wire = GpuCommandIrDetail::ReadWirePod<GpuCommandIrClearBufferRecord>(recordPtr);
        return ValidateBufferClearImpl({ .generation = graphGeneration, .index = wire.destinationResourceIndex }, graph, task);
    }
    case GpuCommandIrWireOpcode::ClearTexture:{
        const auto wire = GpuCommandIrDetail::ReadWirePod<GpuCommandIrClearTextureRecord>(recordPtr);
        const TextureSubresourceSet subresources(wire.destinationSubresources.baseMipLevel, wire.destinationSubresources.numMipLevels, wire.destinationSubresources.baseArraySlice, wire.destinationSubresources.numArraySlices);
        return ValidateTextureClearImpl(
            { .generation = graphGeneration, .index = wire.destinationResourceIndex },
            subresources,
            static_cast<GpuClearTextureTaskValueType::Enum>(wire.clearTextureValueType),
            (static_cast<u8>(wire.clearFlags) & GpuCommandIrClearTextureFlag::ClearDepth) != 0u,
            (static_cast<u8>(wire.clearFlags) & GpuCommandIrClearTextureFlag::ClearStencil) != 0u,
            graph,
            task,
            queue
        );
    }
    case GpuCommandIrWireOpcode::ClearTextureRectUInt:{
        const auto wire = GpuCommandIrDetail::ReadWirePod<GpuCommandIrClearTextureRectUIntRecord>(recordPtr);
        const TextureSubresourceSet subresources(wire.destinationSubresources.baseMipLevel, wire.destinationSubresources.numMipLevels, wire.destinationSubresources.baseArraySlice, wire.destinationSubresources.numArraySlices);
        const Rect clearRect(wire.clearRect.minX, wire.clearRect.maxX, wire.clearRect.minY, wire.clearRect.maxY);
        return ValidateTextureRectUIntClearImpl(
            { .generation = graphGeneration, .index = wire.destinationResourceIndex },
            subresources,
            clearRect,
            graph,
            task,
            queue
        );
    }
    case GpuCommandIrWireOpcode::UploadBuffer:
    case GpuCommandIrWireOpcode::UploadTexture:
        return GpuCommandIrDetail::ValidateUploadWireOperation(opcode, recordPtr, graphGeneration, blobBytes, graph, task, queue);
    default:
        return GpuCommandIrReplayError::InvalidStream;
    }
}



NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

