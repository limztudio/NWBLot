// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/backend_selection/resource_validation.h>
#include <core/graphics/rhi/queue_sharing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Semantic preflight admitted the destination, and the graph stays caller-stable.
GpuCommandIrReplayError::Enum ValidateUploadBackendOperand(const GpuCommandIrBuiltinTaskRecord& record, const GpuTaskGraphDeclarationReadView& graph, CommandList& commandList, const GpuPhysicalQueueInfo& queue)noexcept{
    if(record.opcode == GpuCommandIrOpcode::UploadBuffer){
        auto& destination = *MakeNotNull(graph.bufferForResource(record.destination));
        if(!GraphicsBackend::IsBufferReadyForState(commandList.getDevice(), &destination, ResourceStates::CopyDest) || !ResourceQueueAdmissionAdmitsQueue(destination.getQueueAdmissionSnapshot(), queue))
            return GpuCommandIrReplayError::BackendResourceNotReady;
        const ResourceStates::Mask permanentState = commandList.getPermanentBufferState(&destination);
        if(permanentState != ResourceStates::Unknown && permanentState != record.finalState)
            return GpuCommandIrReplayError::PermanentResourceStateMismatch;
        return GpuCommandIrReplayError::None;
    }
    if(record.opcode == GpuCommandIrOpcode::UploadTexture){
        auto& destination = *MakeNotNull(graph.textureForResource(record.destination));
        if(!GraphicsBackend::IsTextureReadyForState(commandList.getDevice(), &destination, ResourceStates::CopyDest) || !ResourceQueueAdmissionAdmitsQueue(destination.getQueueAdmissionSnapshot(), queue))
            return GpuCommandIrReplayError::BackendResourceNotReady;
        const ResourceStates::Mask permanentState = commandList.getPermanentTextureState(&destination);
        if(permanentState != ResourceStates::Unknown && permanentState != record.finalState)
            return GpuCommandIrReplayError::PermanentResourceStateMismatch;
        return GpuCommandIrReplayError::None;
    }
    return GpuCommandIrReplayError::StreamChangedDuringReplay;
}

// The stream reader admitted this blob range and backend operand preflight admitted its destination on the same stable stream and graph.
bool LowerUploadOperation(const GpuCommandIrBuiltinTaskRecord& record, const BinaryByteView blobBytes, const GpuTaskGraphDeclarationReadView& graph, CommandList& commandList){
    NWB_ASSERT(blobBytes.data() && record.blobSizeBytes != 0u && record.blobOffsetBytes <= static_cast<u64>(blobBytes.size()) && record.blobSizeBytes <= static_cast<u64>(blobBytes.size()) - record.blobOffsetBytes);
    const auto bytes = MakeNotNull(static_cast<const void*>(blobBytes.data() + static_cast<usize>(record.blobOffsetBytes)));
    const usize byteCount = static_cast<usize>(record.blobSizeBytes);
    commandList.endRenderPass();
    if(record.opcode == GpuCommandIrOpcode::UploadBuffer){
        auto& destination = *MakeNotNull(graph.bufferForResource(record.destination));
        const BufferRange range(record.destinationOffsetBytes, record.blobSizeBytes);
        commandList.setBufferState(&destination, ResourceStates::CopyDest, false, range);
        commandList.commitBarriers();
        if(commandList.commandRecordingFailed() || !commandList.tryWriteBuffer(destination, bytes.get(), byteCount, record.destinationOffsetBytes))
            return false;
        if(record.finalState != ResourceStates::CopyDest){
            commandList.setBufferState(&destination, record.finalState, false, range);
            commandList.commitBarriers();
        }
        return !commandList.commandRecordingFailed();
    }
    if(record.opcode == GpuCommandIrOpcode::UploadTexture){
        auto& destination = *MakeNotNull(graph.textureForResource(record.destination));
        const TextureSlice& slice = record.destinationSlice;
        const TextureSubresourceSet subresources(slice.mipLevel, 1u, slice.arraySlice, 1u);
        commandList.setTextureState(&destination, subresources, ResourceStates::CopyDest);
        commandList.commitBarriers();
        if(commandList.commandRecordingFailed() || !commandList.tryWriteTexture(destination, slice.arraySlice, slice.mipLevel, bytes.get(), static_cast<usize>(record.rowPitch), static_cast<usize>(record.depthPitch), record.uploadAspect))
            return false;
        if(record.finalState != ResourceStates::CopyDest){
            commandList.setTextureState(&destination, subresources, record.finalState);
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

