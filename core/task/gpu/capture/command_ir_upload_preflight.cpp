// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/backend_selection/resource_validation.h>
#include <core/task/gpu/task_graph.h>
#include <core/task/gpu/task_graph_builtin_internal.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir_upload_preflight{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 HasValidBufferUploadUses(const GpuTaskGraphTaskView& task, const GpuGraphResourceId resource, const BufferDesc& description, const BufferRange requested, const ResourceStates::Mask finalState)noexcept{
    b8 hasCopyDest = false;
    b8 hasFinalState = false;
    ResourceStates::Mask terminalState = ResourceStates::Unknown;
    for(usize index = 0u; index < task.resourceUseCount; ++index){
        const GpuTaskResourceUse& use = task.resourceUses[index];
        if(use.resource == resource && use.access == GpuTaskResourceAccess::Write && use.range.bufferRange.resolve(description).contains(requested)){
            terminalState = use.requiredState;
            if(use.requiredState == ResourceStates::CopyDest)
                hasCopyDest = true;
            if(use.requiredState == finalState)
                hasFinalState = true;
        }
    }
    return hasCopyDest && hasFinalState && terminalState == finalState;
}

[[nodiscard]] static b8 HasValidTextureUploadUses(const GpuTaskGraphTaskView& task, const GpuGraphResourceId resource, const TextureDesc& description, const TextureSubresourceSet requested, const ResourceStates::Mask finalState)noexcept{
    b8 hasCopyDest = false;
    b8 hasFinalState = false;
    ResourceStates::Mask terminalState = ResourceStates::Unknown;
    for(usize index = 0u; index < task.resourceUseCount; ++index){
        const GpuTaskResourceUse& use = task.resourceUses[index];
        if(use.resource == resource && use.access == GpuTaskResourceAccess::Write && use.range.textureSubresources.resolve(description, TextureSubresourceMipResolve::Range).contains(requested)){
            terminalState = use.requiredState;
            if(use.requiredState == ResourceStates::CopyDest)
                hasCopyDest = true;
            if(use.requiredState == finalState)
                hasFinalState = true;
        }
    }
    return hasCopyDest && hasFinalState && terminalState == finalState;
}

[[nodiscard]] static b8 HasSourceBlobImpl(const GpuUploadBlobId sourceUploadBlob, const u64 blobOffsetBytes, const u64 blobSizeBytes, const BinaryByteView blobBytes, const GpuTaskGraphDeclarationReadView& graph)noexcept{
    if(!graph.validUploadBlob(sourceUploadBlob))
        return false;
    const auto source = graph.uploadBlobData(sourceUploadBlob);
    if(!source || !blobBytes.data() || blobOffsetBytes > static_cast<u64>(blobBytes.size()) || blobSizeBytes > static_cast<u64>(blobBytes.size()) - blobOffsetBytes || blobSizeBytes > static_cast<u64>(source->size()))
        return false;
    return FOUNDATION_MEMCMP(source->data(), blobBytes.data() + static_cast<usize>(blobOffsetBytes), static_cast<usize>(blobSizeBytes)) == 0;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateBufferUploadImpl(
    const GpuGraphResourceId destinationId,
    const GpuUploadBlobId sourceUploadBlob,
    const u64 blobOffsetBytes,
    const u64 blobSizeBytes,
    const u64 destinationOffsetBytes,
    const ResourceStates::Mask finalState,
    const BinaryByteView blobBytes,
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
    if(!HasSourceBlobImpl(sourceUploadBlob, blobOffsetBytes, blobSizeBytes, blobBytes, graph))
        return GpuCommandIrReplayError::InvalidBufferUpload;

    const BufferDesc& description = destination->getCreationDescription();
    if(
        blobSizeBytes == 0u
        || blobSizeBytes > static_cast<u64>(Limit<usize>::s_Max)
        || (destinationOffsetBytes & (sizeof(u32) - 1u)) != 0u
        || (blobSizeBytes & (sizeof(u32) - 1u)) != 0u
        || !GraphicsBackend::IsBufferRangeInBounds(description, destinationOffsetBytes, blobSizeBytes)
        || finalState == ResourceStates::Unknown
        || !GpuTaskGraphBuiltinDetail::BuiltInTaskCanMaterializeRetainedState(description, graph.resourceInitialStateAt(destinationId.index), graph.resourceExternalFinalStateAt(destinationId.index))
        || (description.keepInitialState && finalState != description.initialState)
    )
        return GpuCommandIrReplayError::InvalidBufferUpload;

    const BufferRange requested(destinationOffsetBytes, blobSizeBytes);
    if(!HasValidBufferUploadUses(task, destinationId, description, requested, finalState))
        return GpuCommandIrReplayError::ResourceUseMismatch;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateTextureUploadImpl(
    const GpuGraphResourceId destinationId,
    const GpuUploadBlobId sourceUploadBlob,
    const u64 blobOffsetBytes,
    const u64 blobSizeBytes,
    const TextureSlice& slice,
    const u64 rowPitch,
    const u64 depthPitch,
    const ResourceStates::Mask finalState,
    const TextureUploadAspect::Enum uploadAspect,
    const BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& queue
)noexcept{
    Texture* const destination = graph.textureForResource(destinationId);
    if(!destination){
        if(!graph.validResource(destinationId))
            return GpuCommandIrReplayError::InvalidResource;
        if(graph.resourceTypeAt(destinationId.index) != GpuGraphResourceType::Texture)
            return GpuCommandIrReplayError::ResourceTypeMismatch;
        return GpuCommandIrReplayError::MissingBackendResource;
    }
    if(!HasSourceBlobImpl(sourceUploadBlob, blobOffsetBytes, blobSizeBytes, blobBytes, graph))
        return GpuCommandIrReplayError::InvalidTextureUpload;

    const TextureDesc& description = destination->getCreationDescription();
    Expected<usize> requiredBytes = MakeUnexpected(Failure{});
    if(static_cast<usize>(description.format) >= static_cast<usize>(Format::Count))
        return GpuCommandIrReplayError::InvalidTextureUpload;
    const auto resolvedAspect = ResolveTextureUploadAspect(GetFormatInfo(description.format), uploadAspect);
    if(
        rowPitch > static_cast<u64>(Limit<usize>::s_Max)
        || depthPitch > static_cast<u64>(Limit<usize>::s_Max)
        || finalState == ResourceStates::Unknown
        || !resolvedAspect
        || !GpuTaskGraphBuiltinDetail::UploadTextureTaskCanMaterializeRetainedState(description, graph.resourceInitialStateAt(destinationId.index), graph.resourceExternalFinalStateAt(destinationId.index), finalState)
        || !(requiredBytes = ComputeTextureUploadByteSize(description, slice.arraySlice, slice.mipLevel, static_cast<usize>(rowPitch), static_cast<usize>(depthPitch), uploadAspect))
        || *requiredBytes != blobSizeBytes
    )
        return GpuCommandIrReplayError::InvalidTextureUpload;

    TextureSlice fullSlice;
    fullSlice.mipLevel = slice.mipLevel;
    fullSlice.arraySlice = slice.arraySlice;
    fullSlice = fullSlice.resolve(description);
    if(slice.x != fullSlice.x || slice.y != fullSlice.y || slice.z != fullSlice.z || slice.width != fullSlice.width || slice.height != fullSlice.height || slice.depth != fullSlice.depth)
        return GpuCommandIrReplayError::InvalidTextureUpload;

    const u8 taskCapabilities = static_cast<u8>(task.commands.allowedCapabilities());
    const u8 queueCapabilities = static_cast<u8>(queue.capabilities);
    if(
        (taskCapabilities & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u
        || (queueCapabilities & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u
        || (*resolvedAspect != TextureUploadAspect::Color && ((taskCapabilities & static_cast<u8>(GpuQueueCapability::Graphics)) == 0u || (queueCapabilities & static_cast<u8>(GpuQueueCapability::Graphics)) == 0u))
    )
        return GpuCommandIrReplayError::InvalidTextureUpload;

    const TextureSubresourceSet requested(slice.mipLevel, 1u, slice.arraySlice, 1u);
    if(!HasValidTextureUploadUses(task, destinationId, description, requested, finalState))
        return GpuCommandIrReplayError::ResourceUseMismatch;
    return GpuCommandIrReplayError::None;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrReplayError::Enum ValidateUploadOperation(const GpuCommandIrBuiltinTaskRecord& record, const BinaryByteView blobBytes, const GpuTaskGraphDeclarationReadView& graph, const GpuTaskGraphTaskView& task, const GpuPhysicalQueueInfo& queue)noexcept{
    switch(record.opcode){
    case GpuCommandIrOpcode::UploadBuffer:
        return __hidden_gpu_command_ir_upload_preflight::ValidateBufferUploadImpl(record.destination, record.sourceUploadBlob, record.blobOffsetBytes, record.blobSizeBytes, record.destinationOffsetBytes, record.finalState, blobBytes, graph, task);
    case GpuCommandIrOpcode::UploadTexture:
        return __hidden_gpu_command_ir_upload_preflight::ValidateTextureUploadImpl(record.destination, record.sourceUploadBlob, record.blobOffsetBytes, record.blobSizeBytes, record.destinationSlice, record.rowPitch, record.depthPitch, record.finalState, record.uploadAspect, blobBytes, graph, task, queue);
    default:
        return GpuCommandIrReplayError::InvalidStream;
    }
}

GpuCommandIrReplayError::Enum ValidateUploadWireOperation(
    const GpuCommandIrWireOpcode::Enum opcode,
    const u8* const recordPtr,
    const u64 graphGeneration,
    const BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& queue
)noexcept{
    switch(opcode){
    case GpuCommandIrWireOpcode::UploadBuffer:{
        const auto wire = ReadWirePod<GpuCommandIrUploadBufferRecord>(recordPtr);
        return __hidden_gpu_command_ir_upload_preflight::ValidateBufferUploadImpl(
            { .generation = graphGeneration, .index = wire.destinationResourceIndex },
            { .generation = graphGeneration, .index = wire.sourceUploadBlobIndex },
            wire.blobOffsetBytes,
            wire.blobSizeBytes,
            wire.destinationOffsetBytes,
            static_cast<ResourceStates::Mask>(wire.finalState),
            blobBytes,
            graph,
            task
        );
    }
    case GpuCommandIrWireOpcode::UploadTexture:{
        const auto wire = ReadWirePod<GpuCommandIrUploadTextureRecord>(recordPtr);
        const TextureSlice slice{ .x = wire.destinationSlice.x, .y = wire.destinationSlice.y, .z = wire.destinationSlice.z, .width = wire.destinationSlice.width, .height = wire.destinationSlice.height, .depth = wire.destinationSlice.depth, .mipLevel = wire.destinationSlice.mipLevel, .arraySlice = wire.destinationSlice.arraySlice };
        return __hidden_gpu_command_ir_upload_preflight::ValidateTextureUploadImpl(
            { .generation = graphGeneration, .index = wire.destinationResourceIndex },
            { .generation = graphGeneration, .index = wire.sourceUploadBlobIndex },
            wire.blobOffsetBytes,
            wire.blobSizeBytes,
            slice,
            wire.rowPitch,
            wire.depthPitch,
            static_cast<ResourceStates::Mask>(wire.finalState),
            static_cast<TextureUploadAspect::Enum>(wire.aspect),
            blobBytes,
            graph,
            task,
            queue
        );
    }
    default:
        return GpuCommandIrReplayError::InvalidStream;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

