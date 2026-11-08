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

[[nodiscard]] static b8 TaskDeclaresTextureSlice(
    const GpuTaskGraphTaskView& task,
    const GpuGraphResourceId resource,
    const ResourceStates::Mask state,
    const GpuTaskResourceAccess::Enum access,
    const TextureDesc& description,
    const TextureSlice& resolvedSlice
)noexcept{
    for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
        const GpuTaskResourceUse& use = task.resourceUses[useIndex];
        if(use.resource == resource && use.requiredState == state && use.access == access && TextureSliceIsDeclared(use.range.textureSubresources, description, resolvedSlice))
            return true;
    }
    return false;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateBufferCopyRecord(const GpuCommandIrBuiltinTaskRecord& record, const GpuTaskGraphDeclarationReadView& graph, const GpuTaskGraphTaskView& task)noexcept{
    if(!graph.validResource(record.source) || !graph.validResource(record.destination))
        return GpuCommandIrReplayError::InvalidResource;

    const GpuTaskGraphResourceView sourceView = graph.resourceAt(record.source.index);
    const GpuTaskGraphResourceView destinationView = graph.resourceAt(record.destination.index);
    if(sourceView.type != GpuGraphResourceType::Buffer || destinationView.type != GpuGraphResourceType::Buffer)
        return GpuCommandIrReplayError::ResourceTypeMismatch;

    Buffer* const source = graph.bufferForResource(record.source);
    Buffer* const destination = graph.bufferForResource(record.destination);
    if(!source || !destination)
        return GpuCommandIrReplayError::MissingBackendResource;

    const auto declaresRange = [&](const GpuGraphResourceId resource, const BufferDesc& description, const ResourceStates::Mask state, const GpuTaskResourceAccess::Enum access, const u64 offset)noexcept{
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            if(use.resource == resource && use.requiredState == state && use.access == access && BufferRangeContains(description, use.range.bufferRange, offset, record.dataSizeBytes))
                return true;
        }
        return false;
    };
    if(!GpuCommandIrDetail::FindTaskResourceUse(task, record.source, ResourceStates::CopySource, GpuTaskResourceAccess::Read) || !GpuCommandIrDetail::FindTaskResourceUse(task, record.destination, ResourceStates::CopyDest, GpuTaskResourceAccess::Write))
        return GpuCommandIrReplayError::ResourceUseMismatch;
    if(
        record.dataSizeBytes == 0u
        || !declaresRange(record.source, source->getCreationDescription(), ResourceStates::CopySource, GpuTaskResourceAccess::Read, record.sourceOffsetBytes)
        || !declaresRange(record.destination, destination->getCreationDescription(), ResourceStates::CopyDest, GpuTaskResourceAccess::Write, record.destinationOffsetBytes)
        || (source == destination && UnsignedRangesOverlap(record.sourceOffsetBytes, record.dataSizeBytes, record.destinationOffsetBytes, record.dataSizeBytes))
    )
        return GpuCommandIrReplayError::InvalidBufferCopy;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateTextureCopyRecord(const GpuCommandIrBuiltinTaskRecord& record, const GpuTaskGraphDeclarationReadView& graph, const GpuTaskGraphTaskView& task, const GpuPhysicalQueueInfo& queue)noexcept{
    if(!graph.validResource(record.source) || !graph.validResource(record.destination) || record.source == record.destination)
        return GpuCommandIrReplayError::InvalidResource;

    const GpuTaskGraphResourceView sourceView = graph.resourceAt(record.source.index);
    const GpuTaskGraphResourceView destinationView = graph.resourceAt(record.destination.index);
    if(sourceView.type != GpuGraphResourceType::Texture || destinationView.type != GpuGraphResourceType::Texture)
        return GpuCommandIrReplayError::ResourceTypeMismatch;

    Texture* const source = graph.textureForResource(record.source);
    Texture* const destination = graph.textureForResource(record.destination);
    if(!source || !destination)
        return GpuCommandIrReplayError::MissingBackendResource;
    if(source == destination)
        return GpuCommandIrReplayError::InvalidTextureCopy;

    if(!GpuCommandIrDetail::FindTaskResourceUse(task, record.source, ResourceStates::CopySource, GpuTaskResourceAccess::Read) || !GpuCommandIrDetail::FindTaskResourceUse(task, record.destination, ResourceStates::CopyDest, GpuTaskResourceAccess::Write))
        return GpuCommandIrReplayError::ResourceUseMismatch;

    const TextureDesc& sourceDescription = source->getCreationDescription();
    const TextureDesc& destinationDescription = destination->getCreationDescription();
    Expected<GraphicsBackend::TextureCopyContract> contract = MakeUnexpected(Failure{});
    if(!(contract = GraphicsBackend::ResolveTextureCopyContract(sourceDescription, record.sourceSlice, destinationDescription, record.destinationSlice)))
        return GpuCommandIrReplayError::InvalidTextureCopy;
    if(
        !TaskDeclaresTextureSlice(task, record.source, ResourceStates::CopySource, GpuTaskResourceAccess::Read, sourceDescription, contract->sourceSlice)
        || !TaskDeclaresTextureSlice(task, record.destination, ResourceStates::CopyDest, GpuTaskResourceAccess::Write, destinationDescription, contract->destinationSlice)
    )
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

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateBufferClearRecord(const GpuCommandIrBuiltinTaskRecord& record, const GpuTaskGraphDeclarationReadView& graph, const GpuTaskGraphTaskView& task)noexcept{
    if(!graph.validResource(record.destination))
        return GpuCommandIrReplayError::InvalidResource;

    const GpuTaskGraphResourceView destinationView = graph.resourceAt(record.destination.index);
    if(destinationView.type != GpuGraphResourceType::Buffer)
        return GpuCommandIrReplayError::ResourceTypeMismatch;

    Buffer* const destination = graph.bufferForResource(record.destination);
    if(!destination)
        return GpuCommandIrReplayError::MissingBackendResource;

    const GpuTaskResourceUse* const destinationUse = GpuCommandIrDetail::FindTaskResourceUse(task, record.destination, ResourceStates::CopyDest, GpuTaskResourceAccess::Write);
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

[[nodiscard]] static Expected<TextureClearDestination, GpuCommandIrReplayError::Enum> ResolveTextureClearDestination(const GpuCommandIrBuiltinTaskRecord& record, const GpuTaskGraphDeclarationReadView& graph, const GpuTaskGraphTaskView& task)noexcept{
    if(!graph.validResource(record.destination))
        return MakeUnexpected(GpuCommandIrReplayError::InvalidResource);

    const GpuTaskGraphResourceView destinationView = graph.resourceAt(record.destination.index);
    if(destinationView.type != GpuGraphResourceType::Texture)
        return MakeUnexpected(GpuCommandIrReplayError::ResourceTypeMismatch);

    Texture* const destination = graph.textureForResource(record.destination);
    if(!destination)
        return MakeUnexpected(GpuCommandIrReplayError::MissingBackendResource);

    const GpuTaskResourceUse* const destinationUse = GpuCommandIrDetail::FindTaskResourceUse(task, record.destination, ResourceStates::CopyDest, GpuTaskResourceAccess::Write);
    if(!destinationUse)
        return MakeUnexpected(GpuCommandIrReplayError::ResourceUseMismatch);
    return TextureClearDestination{ .texture = *destination, .use = *destinationUse, };
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateTextureClearRecord(const GpuCommandIrBuiltinTaskRecord& record, const GpuTaskGraphDeclarationReadView& graph, const GpuTaskGraphTaskView& task, const GpuPhysicalQueueInfo& queue)noexcept{
    const auto resolvedDestination = ResolveTextureClearDestination(record, graph, task);
    if(!resolvedDestination)
        return resolvedDestination.error();
    Texture& destination = resolvedDestination->texture;
    const GpuTaskResourceUse& destinationUse = resolvedDestination->use;

    const TextureDesc& description = destination.getCreationDescription();
    const auto valueKind = GpuTaskGraphClearDetail::TryMapTextureClearValueKind(record.clearTextureValueType);
    Expected<GraphicsBackend::TextureClearContract> clearContract = MakeUnexpected(Failure{});
    if(
        !TextureSubresourcesAreCanonical(description, record.destinationSubresources)
        || !TextureSubresourcesContain(destinationUse.range.textureSubresources, description, record.destinationSubresources)
        || !valueKind
        || !(clearContract = GraphicsBackend::ResolveTextureClearContract(description, record.destinationSubresources, *valueKind, record.clearDepth, record.clearStencil))
        || clearContract->subresources != record.destinationSubresources
        || !GraphicsBackend::TextureClearQueueRequirementSatisfied(clearContract->queueRequirement, task.commands.allowedCapabilities(), queue.capabilities)
    )
        return GpuCommandIrReplayError::InvalidTextureClear;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateTextureRectUIntClearRecord(const GpuCommandIrBuiltinTaskRecord& record, const GpuTaskGraphDeclarationReadView& graph, const GpuTaskGraphTaskView& task, const GpuPhysicalQueueInfo& queue)noexcept{
    const auto resolvedDestination = ResolveTextureClearDestination(record, graph, task);
    if(!resolvedDestination)
        return resolvedDestination.error();
    Texture& destination = resolvedDestination->texture;
    const GpuTaskResourceUse& destinationUse = resolvedDestination->use;

    const TextureDesc& description = destination.getCreationDescription();
    Expected<GraphicsBackend::TextureClearContract> clearContract = MakeUnexpected(Failure{});
    const Box clearBox(record.clearRect, 0, Limit<i32>::s_Max);
    if(
        record.clearRect.maxX <= record.clearRect.minX
        || record.clearRect.maxY <= record.clearRect.minY
        || description.sampleCount != 1u
        || !TextureSubresourcesAreCanonical(description, record.destinationSubresources)
        || !TextureSubresourcesContain(destinationUse.range.textureSubresources, description, record.destinationSubresources)
        || !(clearContract = GraphicsBackend::ResolveTextureClearContract(description, record.destinationSubresources, GraphicsBackend::TextureClearValueKind::UInt, false, false))
        || clearContract->subresources != record.destinationSubresources
        || !GraphicsBackend::TextureClearQueueRequirementSatisfied(GraphicsBackend::TextureClearBoxQueueRequirement(description, clearContract->subresources, clearBox), task.commands.allowedCapabilities(), queue.capabilities)
    )
        return GpuCommandIrReplayError::InvalidTextureClear;
    return GpuCommandIrReplayError::None;
}

};


GpuCommandIrReplayError::Enum GpuCommandIrBuiltinPreflight::validate(
    const GpuCommandIrBuiltinTaskRecord& record,
    const BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& queue
)noexcept{
    using namespace __hidden_gpu_command_ir_builtin_preflight;
    switch(record.opcode){
    case GpuCommandIrOpcode::CopyBuffer:
        return ValidateBufferCopyRecord(record, graph, task);
    case GpuCommandIrOpcode::CopyTexture:
        return ValidateTextureCopyRecord(record, graph, task, queue);
    case GpuCommandIrOpcode::ClearBuffer:
        return ValidateBufferClearRecord(record, graph, task);
    case GpuCommandIrOpcode::ClearTexture:
        return ValidateTextureClearRecord(record, graph, task, queue);
    case GpuCommandIrOpcode::ClearTextureRectUInt:
        return ValidateTextureRectUIntClearRecord(record, graph, task, queue);
    case GpuCommandIrOpcode::UploadBuffer:
    case GpuCommandIrOpcode::UploadTexture:
        return GpuCommandIrDetail::ValidateUploadOperation(record, blobBytes, graph, task, queue);
    default:
        return GpuCommandIrReplayError::InvalidStream;
    }
}



NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
