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

template<typename ResourceT, GpuGraphResourceType::Enum ExpectedType, typename LookupFn>
[[nodiscard]] static Expected<ResourceT*, GpuCommandIrReplayError::Enum> ResolveGraphResourceImpl(
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuGraphResourceId resourceId,
    LookupFn&& lookup
)noexcept{
    if(ResourceT* const resource = lookup(graph, resourceId))
        return resource;
    if(!graph.validResource(resourceId))
        return MakeUnexpected(GpuCommandIrReplayError::InvalidResource);
    if(graph.resourceTypeAt(resourceId.index) != ExpectedType)
        return MakeUnexpected(GpuCommandIrReplayError::ResourceTypeMismatch);
    return MakeUnexpected(GpuCommandIrReplayError::MissingBackendResource);
}

template<typename ResourceT, GpuGraphResourceType::Enum ExpectedType, typename LookupFn>
[[nodiscard]] static Expected<Pair<ResourceT*, ResourceT*>, GpuCommandIrReplayError::Enum> ResolveGraphResourcePairImpl(
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuGraphResourceId firstId,
    const GpuGraphResourceId secondId,
    LookupFn&& lookup
)noexcept{
    const auto first = ResolveGraphResourceImpl<ResourceT, ExpectedType>(graph, firstId, lookup);
    const auto second = ResolveGraphResourceImpl<ResourceT, ExpectedType>(graph, secondId, lookup);
    if(!first && !second)
        return MakeUnexpected(Min(first.error(), second.error()));
    if(!first)
        return MakeUnexpected(first.error());
    if(!second)
        return MakeUnexpected(second.error());
    return Pair<ResourceT*, ResourceT*>{ *first, *second };
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
    const auto resolvedBuffers = ResolveGraphResourcePairImpl<Buffer, GpuGraphResourceType::Buffer>(
        graph,
        sourceId,
        destinationId,
        [](const GpuTaskGraphDeclarationReadView& view, const GpuGraphResourceId id)noexcept{ return view.bufferForResource(id); }
    );
    if(!resolvedBuffers)
        return resolvedBuffers.error();
    Buffer* const source = resolvedBuffers->first();
    Buffer* const destination = resolvedBuffers->second();

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
    const auto resolvedTextures = ResolveGraphResourcePairImpl<Texture, GpuGraphResourceType::Texture>(
        graph,
        sourceId,
        destinationId,
        [](const GpuTaskGraphDeclarationReadView& view, const GpuGraphResourceId id)noexcept{ return view.textureForResource(id); }
    );
    if(!resolvedTextures)
        return resolvedTextures.error();
    Texture* const source = resolvedTextures->first();
    Texture* const destination = resolvedTextures->second();
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

    if(
        contract->queueRequirement != GraphicsBackend::TextureQueueRequirement::Transfer
        && !GraphicsBackend::TextureQueueRequirementSatisfied(contract->queueRequirement, task.commands.allowedCapabilities(), queue.capabilities)
    )
        return GpuCommandIrReplayError::InvalidTextureCopy;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateBufferClearImpl(
    const GpuGraphResourceId destinationId,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task
)noexcept{
    const auto destinationResult = GpuCommandIrDetail::ResolveGraphBuffer(graph, destinationId);
    if(!destinationResult)
        return destinationResult.error();
    Buffer* const destination = *destinationResult;

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
    const auto destinationResult = GpuCommandIrDetail::ResolveGraphTexture(graph, destinationId);
    if(!destinationResult)
        return MakeUnexpected(destinationResult.error());
    Texture* const destination = *destinationResult;

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
        || !GraphicsBackend::TextureQueueRequirementSatisfied(clearContract->queueRequirement, task.commands.allowedCapabilities(), queue.capabilities)
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
        || !GraphicsBackend::TextureQueueRequirementSatisfied(GraphicsBackend::TextureClearBoxQueueRequirement(description, clearContract->subresources, clearBox), task.commands.allowedCapabilities(), queue.capabilities)
    )
        return GpuCommandIrReplayError::InvalidTextureClear;
    return GpuCommandIrReplayError::None;
}

};


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Buffer*, GpuCommandIrReplayError::Enum> ResolveGraphBuffer(const GpuTaskGraphDeclarationReadView& graph, const GpuGraphResourceId resourceId)noexcept{
    return __hidden_gpu_command_ir_builtin_preflight::ResolveGraphResourceImpl<Buffer, GpuGraphResourceType::Buffer>(
        graph,
        resourceId,
        [](const GpuTaskGraphDeclarationReadView& view, const GpuGraphResourceId id)noexcept{ return view.bufferForResource(id); }
    );
}

Expected<Texture*, GpuCommandIrReplayError::Enum> ResolveGraphTexture(const GpuTaskGraphDeclarationReadView& graph, const GpuGraphResourceId resourceId)noexcept{
    return __hidden_gpu_command_ir_builtin_preflight::ResolveGraphResourceImpl<Texture, GpuGraphResourceType::Texture>(
        graph,
        resourceId,
        [](const GpuTaskGraphDeclarationReadView& view, const GpuGraphResourceId id)noexcept{ return view.textureForResource(id); }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

