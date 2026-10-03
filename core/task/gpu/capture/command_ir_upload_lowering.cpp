// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"

#include <core/graphics/vulkan/backend_context.h>
#include <core/graphics/rhi/queue_sharing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrReplayError::Enum ValidateUploadBackendOperand(
    const GpuCommandIrBuiltinTaskRecord& record,
    const GpuTaskGraphDeclarationReadView& graph,
    CommandList& commandList,
    const GpuPhysicalQueueInfo& queue
)noexcept{
    if(record.opcode == GpuCommandIrOpcode::UploadBuffer){
        Buffer* const destination = graph.bufferForResource(record.destination);
        if(!destination)
            return GpuCommandIrReplayError::StreamChangedDuringReplay;
        if(
            !commandList.getDevice().isBufferReadyForGpuUse(destination, VK_BUFFER_USAGE_TRANSFER_DST_BIT)
            || !ResourceQueueAdmissionAdmitsQueue(destination->getQueueAdmissionSnapshot(), queue)
        )
            return GpuCommandIrReplayError::BackendResourceNotReady;
        const ResourceStates::Mask permanentState = commandList.getPermanentBufferState(destination);
        if(permanentState != ResourceStates::Unknown && permanentState != record.finalState)
            return GpuCommandIrReplayError::PermanentResourceStateMismatch;
        return GpuCommandIrReplayError::None;
    }
    if(record.opcode == GpuCommandIrOpcode::UploadTexture){
        Texture* const destination = graph.textureForResource(record.destination);
        if(!destination)
            return GpuCommandIrReplayError::StreamChangedDuringReplay;
        if(
            !commandList.getDevice().isTextureReadyForGpuUse(destination, VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            || !ResourceQueueAdmissionAdmitsQueue(destination->getQueueAdmissionSnapshot(), queue)
        )
            return GpuCommandIrReplayError::BackendResourceNotReady;
        const ResourceStates::Mask permanentState = commandList.getPermanentTextureState(destination);
        if(permanentState != ResourceStates::Unknown && permanentState != record.finalState)
            return GpuCommandIrReplayError::PermanentResourceStateMismatch;
        return GpuCommandIrReplayError::None;
    }
    return GpuCommandIrReplayError::StreamChangedDuringReplay;
}

bool LowerUploadOperation(
    const GpuCommandIrBuiltinTaskRecord& record,
    const BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    CommandList& commandList
)noexcept{
    if(
        !blobBytes.data()
        || record.blobSizeBytes == 0u
        || record.blobOffsetBytes > static_cast<u64>(blobBytes.size())
        || record.blobSizeBytes > static_cast<u64>(blobBytes.size()) - record.blobOffsetBytes
    )
        return false;
    const void* const bytes = blobBytes.data() + static_cast<usize>(record.blobOffsetBytes);
    const usize byteCount = static_cast<usize>(record.blobSizeBytes);
    commandList.endRenderPass();
    if(record.opcode == GpuCommandIrOpcode::UploadBuffer){
        Buffer* const destination = graph.bufferForResource(record.destination);
        if(!destination)
            return false;
        const BufferRange range(record.destinationOffsetBytes, record.blobSizeBytes);
        commandList.setBufferState(destination, ResourceStates::CopyDest, false, range);
        commandList.commitBarriers();
        if(
            commandList.commandRecordingFailed()
            || !commandList.tryWriteBuffer(*destination, bytes, byteCount, record.destinationOffsetBytes)
        )
            return false;
        if(record.finalState != ResourceStates::CopyDest){
            commandList.setBufferState(destination, record.finalState, false, range);
            commandList.commitBarriers();
        }
        return !commandList.commandRecordingFailed();
    }
    if(record.opcode == GpuCommandIrOpcode::UploadTexture){
        Texture* const destination = graph.textureForResource(record.destination);
        if(!destination)
            return false;
        const TextureSlice& slice = record.destinationSlice;
        const TextureSubresourceSet subresources(slice.mipLevel, 1u, slice.arraySlice, 1u);
        commandList.setTextureState(destination, subresources, ResourceStates::CopyDest);
        commandList.commitBarriers();
        if(
            commandList.commandRecordingFailed()
            || !commandList.tryWriteTexture(
                *destination,
                slice.arraySlice,
                slice.mipLevel,
                bytes,
                static_cast<usize>(record.rowPitch),
                static_cast<usize>(record.depthPitch),
                record.uploadAspect
            )
        )
            return false;
        if(record.finalState != ResourceStates::CopyDest){
            commandList.setTextureState(destination, subresources, record.finalState);
            commandList.commitBarriers();
        }
        return !commandList.commandRecordingFailed();
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

