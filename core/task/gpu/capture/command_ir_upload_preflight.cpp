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


[[nodiscard]] static bool DeclaresBufferWrite(
    const GpuTaskGraphTaskView& task,
    const GpuGraphResourceId resource,
    const BufferDesc& description,
    const ResourceStates::Mask state,
    const BufferRange requested
)noexcept{
    for(usize index = 0u; index < task.resourceUseCount; ++index){
        const GpuTaskResourceUse& use = task.resourceUses[index];
        if(
            use.resource == resource
            && use.requiredState == state
            && use.access == GpuTaskResourceAccess::Write
            && use.range.bufferRange.resolve(description).contains(requested)
        )
            return true;
    }
    return false;
}

[[nodiscard]] static bool DeclaresTextureWrite(
    const GpuTaskGraphTaskView& task,
    const GpuGraphResourceId resource,
    const TextureDesc& description,
    const ResourceStates::Mask state,
    const TextureSubresourceSet requested
)noexcept{
    for(usize index = 0u; index < task.resourceUseCount; ++index){
        const GpuTaskResourceUse& use = task.resourceUses[index];
        if(
            use.resource == resource
            && use.requiredState == state
            && use.access == GpuTaskResourceAccess::Write
            && use.range.textureSubresources.resolve(description, TextureSubresourceMipResolve::Range).contains(requested)
        )
            return true;
    }
    return false;
}

[[nodiscard]] static bool DeclaresTerminalBufferState(
    const GpuTaskGraphTaskView& task,
    const GpuGraphResourceId resource,
    const BufferDesc& description,
    const BufferRange requested,
    const ResourceStates::Mask finalState
)noexcept{
    ResourceStates::Mask terminalState = ResourceStates::Unknown;
    for(usize index = 0u; index < task.resourceUseCount; ++index){
        const GpuTaskResourceUse& use = task.resourceUses[index];
        if(
            use.resource == resource
            && use.access == GpuTaskResourceAccess::Write
            && use.range.bufferRange.resolve(description).contains(requested)
        )
            terminalState = use.requiredState;
    }
    return terminalState == finalState;
}

[[nodiscard]] static bool DeclaresTerminalTextureState(
    const GpuTaskGraphTaskView& task,
    const GpuGraphResourceId resource,
    const TextureDesc& description,
    const TextureSubresourceSet requested,
    const ResourceStates::Mask finalState
)noexcept{
    ResourceStates::Mask terminalState = ResourceStates::Unknown;
    for(usize index = 0u; index < task.resourceUseCount; ++index){
        const GpuTaskResourceUse& use = task.resourceUses[index];
        if(
            use.resource == resource
            && use.access == GpuTaskResourceAccess::Write
            && use.range.textureSubresources.resolve(description, TextureSubresourceMipResolve::Range).contains(requested)
        )
            terminalState = use.requiredState;
    }
    return terminalState == finalState;
}

[[nodiscard]] static bool HasSourceBlob(
    const GpuCommandIrBuiltinTaskRecord& record,
    const BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph
)noexcept{
    if(!graph.validUploadBlob(record.sourceUploadBlob))
        return false;
    usize sourceSize = 0u;
    const void* const source = graph.uploadBlobData(record.sourceUploadBlob, sourceSize);
    if(
        !source
        || !blobBytes.data()
        || record.blobOffsetBytes > static_cast<u64>(blobBytes.size())
        || record.blobSizeBytes > static_cast<u64>(blobBytes.size()) - record.blobOffsetBytes
        || record.blobSizeBytes > static_cast<u64>(sourceSize)
    )
        return false;
    return GLB_MEMCMP(
        source,
        blobBytes.data() + static_cast<usize>(record.blobOffsetBytes),
        static_cast<usize>(record.blobSizeBytes)
    ) == 0;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateBufferUpload(
    const GpuCommandIrBuiltinTaskRecord& record,
    const BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task
)noexcept{
    if(!graph.validResource(record.destination))
        return GpuCommandIrReplayError::InvalidResource;
    const GpuTaskGraphResourceView view = graph.resourceAt(record.destination.index);
    if(view.type != GpuGraphResourceType::Buffer)
        return GpuCommandIrReplayError::ResourceTypeMismatch;
    Buffer* const destination = graph.bufferForResource(record.destination);
    if(!destination)
        return GpuCommandIrReplayError::MissingBackendResource;
    if(!HasSourceBlob(record, blobBytes, graph))
        return GpuCommandIrReplayError::InvalidBufferUpload;

    const BufferDesc& description = destination->getCreationDescription();
    if(
        record.blobSizeBytes == 0u
        || record.blobSizeBytes > static_cast<u64>(Limit<usize>::s_Max)
        || (record.destinationOffsetBytes & (sizeof(u32) - 1u)) != 0u
        || (record.blobSizeBytes & (sizeof(u32) - 1u)) != 0u
        || !GraphicsBackend::IsBufferRangeInBounds(
            description,
            record.destinationOffsetBytes,
            record.blobSizeBytes
        )
        || record.finalState == ResourceStates::Unknown
        || !GpuTaskGraphBuiltinDetail::BuiltInTaskCanMaterializeRetainedState(
            description,
            view.initialState,
            view.externalFinalState
        )
        || (description.keepInitialState && record.finalState != description.initialState)
    )
        return GpuCommandIrReplayError::InvalidBufferUpload;

    const BufferRange requested(record.destinationOffsetBytes, record.blobSizeBytes);
    if(
        !DeclaresBufferWrite(task, record.destination, description, ResourceStates::CopyDest, requested)
        || !DeclaresBufferWrite(task, record.destination, description, record.finalState, requested)
        || !DeclaresTerminalBufferState(task, record.destination, description, requested, record.finalState)
    )
        return GpuCommandIrReplayError::ResourceUseMismatch;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateTextureUpload(
    const GpuCommandIrBuiltinTaskRecord& record,
    const BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& queue
)noexcept{
    if(!graph.validResource(record.destination))
        return GpuCommandIrReplayError::InvalidResource;
    const GpuTaskGraphResourceView view = graph.resourceAt(record.destination.index);
    if(view.type != GpuGraphResourceType::Texture)
        return GpuCommandIrReplayError::ResourceTypeMismatch;
    Texture* const destination = graph.textureForResource(record.destination);
    if(!destination)
        return GpuCommandIrReplayError::MissingBackendResource;
    if(!HasSourceBlob(record, blobBytes, graph))
        return GpuCommandIrReplayError::InvalidTextureUpload;

    const TextureDesc& description = destination->getCreationDescription();
    const TextureSlice& slice = record.destinationSlice;
    usize requiredBytes = 0u;
    TextureUploadAspect::Enum resolvedAspect;
    if(
        static_cast<usize>(description.format) >= static_cast<usize>(Format::kCount)
        ||
        record.rowPitch > static_cast<u64>(Limit<usize>::s_Max)
        || record.depthPitch > static_cast<u64>(Limit<usize>::s_Max)
        || record.finalState == ResourceStates::Unknown
        || !ResolveTextureUploadAspect(GetFormatInfo(description.format), record.uploadAspect, resolvedAspect)
        || !GpuTaskGraphBuiltinDetail::UploadTextureTaskCanMaterializeRetainedState(
            description,
            view.initialState,
            view.externalFinalState,
            record.finalState
        )
        || !GpuTaskGraphBuiltinDetail::ComputeTextureUploadByteSize(
            description,
            slice.arraySlice,
            slice.mipLevel,
            static_cast<usize>(record.rowPitch),
            static_cast<usize>(record.depthPitch),
            record.uploadAspect,
            requiredBytes
        )
        || requiredBytes != record.blobSizeBytes
    )
        return GpuCommandIrReplayError::InvalidTextureUpload;

    TextureSlice fullSlice;
    fullSlice.mipLevel = slice.mipLevel;
    fullSlice.arraySlice = slice.arraySlice;
    fullSlice = fullSlice.resolve(description);
    if(
        slice.x != fullSlice.x
        || slice.y != fullSlice.y
        || slice.z != fullSlice.z
        || slice.width != fullSlice.width
        || slice.height != fullSlice.height
        || slice.depth != fullSlice.depth
    )
        return GpuCommandIrReplayError::InvalidTextureUpload;

    const u8 taskCapabilities = static_cast<u8>(task.commands.allowedCapabilities());
    const u8 queueCapabilities = static_cast<u8>(queue.capabilities);
    if(
        (taskCapabilities & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u
        || (queueCapabilities & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u
        || (
            resolvedAspect != TextureUploadAspect::Color
            && (
                (taskCapabilities & static_cast<u8>(GpuQueueCapability::Graphics)) == 0u
                || (queueCapabilities & static_cast<u8>(GpuQueueCapability::Graphics)) == 0u
            )
        )
    )
        return GpuCommandIrReplayError::InvalidTextureUpload;

    const TextureSubresourceSet requested(slice.mipLevel, 1u, slice.arraySlice, 1u);
    if(
        !DeclaresTextureWrite(task, record.destination, description, ResourceStates::CopyDest, requested)
        || !DeclaresTextureWrite(task, record.destination, description, record.finalState, requested)
        || !DeclaresTerminalTextureState(task, record.destination, description, requested, record.finalState)
    )
        return GpuCommandIrReplayError::ResourceUseMismatch;
    return GpuCommandIrReplayError::None;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrReplayError::Enum ValidateUploadOperation(
    const GpuCommandIrBuiltinTaskRecord& record,
    const BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& queue
)noexcept{
    switch(record.opcode){
    case GpuCommandIrOpcode::UploadBuffer:
        return __hidden_gpu_command_ir_upload_preflight::ValidateBufferUpload(record, blobBytes, graph, task);
    case GpuCommandIrOpcode::UploadTexture:
        return __hidden_gpu_command_ir_upload_preflight::ValidateTextureUpload(record, blobBytes, graph, task, queue);
    default:
        return GpuCommandIrReplayError::InvalidStream;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

