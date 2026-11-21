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


namespace __hidden_gpu_command_ir_upload_lowering{


[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateUploadBufferBackendImpl(const GpuGraphResourceId destinationId, const ResourceStates::Mask finalState, const GpuTaskGraphDeclarationReadView& graph, const GpuPhysicalQueueInfo& queue, CommandList& commandList)noexcept{
    auto& destination = *MakeNotNull(graph.bufferForResource(destinationId));
    if(!GraphicsBackend::IsBufferReadyForState(commandList.getDevice(), &destination, ResourceStates::CopyDest) || !ResourceQueueAdmissionAdmitsQueue(destination.getQueueAdmissionSnapshot(), queue))
        return GpuCommandIrReplayError::BackendResourceNotReady;
    const ResourceStates::Mask permanentState = commandList.getPermanentBufferState(&destination);
    if(permanentState != ResourceStates::Unknown && permanentState != finalState)
        return GpuCommandIrReplayError::PermanentResourceStateMismatch;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateUploadTextureBackendImpl(const GpuGraphResourceId destinationId, const ResourceStates::Mask finalState, const GpuTaskGraphDeclarationReadView& graph, const GpuPhysicalQueueInfo& queue, CommandList& commandList)noexcept{
    auto& destination = *MakeNotNull(graph.textureForResource(destinationId));
    if(!GraphicsBackend::IsTextureReadyForState(commandList.getDevice(), &destination, ResourceStates::CopyDest) || !ResourceQueueAdmissionAdmitsQueue(destination.getQueueAdmissionSnapshot(), queue))
        return GpuCommandIrReplayError::BackendResourceNotReady;
    const ResourceStates::Mask permanentState = commandList.getPermanentTextureState(&destination);
    if(permanentState != ResourceStates::Unknown && permanentState != finalState)
        return GpuCommandIrReplayError::PermanentResourceStateMismatch;
    return GpuCommandIrReplayError::None;
}

[[nodiscard]] static b8 LowerUploadBufferImpl(const GpuGraphResourceId destinationId, const u64 blobOffsetBytes, const u64 blobSizeBytes, const u64 destinationOffsetBytes, const ResourceStates::Mask finalState, const BinaryByteView blobBytes, const GpuTaskGraphDeclarationReadView& graph, CommandList& commandList){
    FOUNDATION_ASSERT(blobBytes.data() && blobSizeBytes != 0u && blobOffsetBytes <= static_cast<u64>(blobBytes.size()) && blobSizeBytes <= static_cast<u64>(blobBytes.size()) - blobOffsetBytes);
    const auto bytes = MakeNotNull(static_cast<const void*>(blobBytes.data() + static_cast<usize>(blobOffsetBytes)));
    const usize byteCount = static_cast<usize>(blobSizeBytes);
    commandList.endRenderPass();
    auto& destination = *MakeNotNull(graph.bufferForResource(destinationId));
    const BufferRange range(destinationOffsetBytes, blobSizeBytes);
    commandList.setBufferState(&destination, ResourceStates::CopyDest, false, range);
    commandList.commitBarriers();
    if(commandList.commandRecordingFailed() || !commandList.tryWriteBuffer(destination, bytes.get(), byteCount, destinationOffsetBytes))
        return false;
    if(finalState != ResourceStates::CopyDest){
        commandList.setBufferState(&destination, finalState, false, range);
        commandList.commitBarriers();
    }
    return !commandList.commandRecordingFailed();
}

[[nodiscard]] static b8 LowerUploadTextureImpl(const GpuGraphResourceId destinationId, const u64 blobOffsetBytes, const u64 blobSizeBytes, const TextureSlice& slice, const u64 rowPitch, const u64 depthPitch, const TextureUploadAspect::Enum uploadAspect, const ResourceStates::Mask finalState, const BinaryByteView blobBytes, const GpuTaskGraphDeclarationReadView& graph, CommandList& commandList){
    FOUNDATION_ASSERT(blobBytes.data() && blobSizeBytes != 0u && blobOffsetBytes <= static_cast<u64>(blobBytes.size()) && blobSizeBytes <= static_cast<u64>(blobBytes.size()) - blobOffsetBytes);
    const auto bytes = MakeNotNull(static_cast<const void*>(blobBytes.data() + static_cast<usize>(blobOffsetBytes)));
    commandList.endRenderPass();
    auto& destination = *MakeNotNull(graph.textureForResource(destinationId));
    const TextureSubresourceSet subresources(slice.mipLevel, 1u, slice.arraySlice, 1u);
    commandList.setTextureState(&destination, subresources, ResourceStates::CopyDest);
    commandList.commitBarriers();
    if(commandList.commandRecordingFailed() || !commandList.tryWriteTexture(destination, slice.arraySlice, slice.mipLevel, bytes.get(), static_cast<usize>(rowPitch), static_cast<usize>(depthPitch), uploadAspect))
        return false;
    if(finalState != ResourceStates::CopyDest){
        commandList.setTextureState(&destination, subresources, finalState);
        commandList.commitBarriers();
    }
    return !commandList.commandRecordingFailed();
}


};


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrReplayError::Enum ValidateUploadBackendOperand(const GpuCommandIrBuiltinTaskRecord& record, const GpuTaskGraphDeclarationReadView& graph, const GpuPhysicalQueueInfo& queue, CommandList& commandList)noexcept{
    if(record.opcode == GpuCommandIrOpcode::UploadBuffer)
        return __hidden_gpu_command_ir_upload_lowering::ValidateUploadBufferBackendImpl(record.destination, record.finalState, graph, queue, commandList);
    if(record.opcode == GpuCommandIrOpcode::UploadTexture)
        return __hidden_gpu_command_ir_upload_lowering::ValidateUploadTextureBackendImpl(record.destination, record.finalState, graph, queue, commandList);
    return GpuCommandIrReplayError::StreamChangedDuringReplay;
}

GpuCommandIrReplayError::Enum ValidateUploadWireBackendOperand(
    const GpuCommandIrWireOpcode::Enum opcode,
    const u8* const recordPtr,
    const u64 graphGeneration,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuPhysicalQueueInfo& queue,
    CommandList& commandList
)noexcept{
    if(opcode == GpuCommandIrWireOpcode::UploadBuffer){
        const auto wire = ReadWirePod<GpuCommandIrUploadBufferRecord>(recordPtr);
        return __hidden_gpu_command_ir_upload_lowering::ValidateUploadBufferBackendImpl({ .generation = graphGeneration, .index = wire.destinationResourceIndex }, static_cast<ResourceStates::Mask>(wire.finalState), graph, queue, commandList);
    }
    if(opcode == GpuCommandIrWireOpcode::UploadTexture){
        const auto wire = ReadWirePod<GpuCommandIrUploadTextureRecord>(recordPtr);
        return __hidden_gpu_command_ir_upload_lowering::ValidateUploadTextureBackendImpl({ .generation = graphGeneration, .index = wire.destinationResourceIndex }, static_cast<ResourceStates::Mask>(wire.finalState), graph, queue, commandList);
    }
    return GpuCommandIrReplayError::StreamChangedDuringReplay;
}

b8 LowerUploadOperation(const GpuCommandIrBuiltinTaskRecord& record, const BinaryByteView blobBytes, const GpuTaskGraphDeclarationReadView& graph, CommandList& commandList){
    if(record.opcode == GpuCommandIrOpcode::UploadBuffer)
        return __hidden_gpu_command_ir_upload_lowering::LowerUploadBufferImpl(record.destination, record.blobOffsetBytes, record.blobSizeBytes, record.destinationOffsetBytes, record.finalState, blobBytes, graph, commandList);
    if(record.opcode == GpuCommandIrOpcode::UploadTexture)
        return __hidden_gpu_command_ir_upload_lowering::LowerUploadTextureImpl(record.destination, record.blobOffsetBytes, record.blobSizeBytes, record.destinationSlice, record.rowPitch, record.depthPitch, record.uploadAspect, record.finalState, blobBytes, graph, commandList);
    return false;
}

b8 LowerUploadWireOperation(
    const GpuCommandIrWireOpcode::Enum opcode,
    const u8* const recordPtr,
    const u64 graphGeneration,
    const BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    CommandList& commandList
){
    if(opcode == GpuCommandIrWireOpcode::UploadBuffer){
        const auto wire = ReadWirePod<GpuCommandIrUploadBufferRecord>(recordPtr);
        return __hidden_gpu_command_ir_upload_lowering::LowerUploadBufferImpl(
            { .generation = graphGeneration, .index = wire.destinationResourceIndex },
            wire.blobOffsetBytes,
            wire.blobSizeBytes,
            wire.destinationOffsetBytes,
            static_cast<ResourceStates::Mask>(wire.finalState),
            blobBytes,
            graph,
            commandList
        );
    }
    if(opcode == GpuCommandIrWireOpcode::UploadTexture){
        const auto wire = ReadWirePod<GpuCommandIrUploadTextureRecord>(recordPtr);
        const TextureSlice slice{ .x = wire.destinationSlice.x, .y = wire.destinationSlice.y, .z = wire.destinationSlice.z, .width = wire.destinationSlice.width, .height = wire.destinationSlice.height, .depth = wire.destinationSlice.depth, .mipLevel = wire.destinationSlice.mipLevel, .arraySlice = wire.destinationSlice.arraySlice };
        return __hidden_gpu_command_ir_upload_lowering::LowerUploadTextureImpl(
            { .generation = graphGeneration, .index = wire.destinationResourceIndex },
            wire.blobOffsetBytes,
            wire.blobSizeBytes,
            slice,
            wire.rowPitch,
            wire.depthPitch,
            static_cast<TextureUploadAspect::Enum>(wire.aspect),
            static_cast<ResourceStates::Mask>(wire.finalState),
            blobBytes,
            graph,
            commandList
        );
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

