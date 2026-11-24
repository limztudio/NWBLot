// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_raster.h"
#include "command_ir_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir_raster_reader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static GpuCommandIrWireOpcode::Enum BuiltinWireOpcode(const GpuCommandIrOpcode::Enum opcode)noexcept{
    switch(opcode){
    case GpuCommandIrOpcode::CopyBuffer: return GpuCommandIrWireOpcode::CopyBuffer;
    case GpuCommandIrOpcode::CopyTexture: return GpuCommandIrWireOpcode::CopyTexture;
    case GpuCommandIrOpcode::ClearBuffer: return GpuCommandIrWireOpcode::ClearBuffer;
    case GpuCommandIrOpcode::ClearTexture: return GpuCommandIrWireOpcode::ClearTexture;
    case GpuCommandIrOpcode::ClearTextureRectUInt: return GpuCommandIrWireOpcode::ClearTextureRectUInt;
    case GpuCommandIrOpcode::UploadBuffer: return GpuCommandIrWireOpcode::UploadBuffer;
    case GpuCommandIrOpcode::UploadTexture: return GpuCommandIrWireOpcode::UploadTexture;
    default: return GpuCommandIrWireOpcode::kCount;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<GpuCommandIrDecodedRecord, GpuCommandIrStreamReadStatus::Enum> GpuCommandIrStreamReader::next()noexcept{
    if(m_validation.failed())
        return MakeUnexpected(GpuCommandIrStreamReadStatus::Error);
    if(m_nextRecordIndex == m_recordCount || m_payloadEnd - m_cursor < sizeof(GpuCommandIrHeader)){
        const auto builtin = nextBuiltinTask();
        if(!builtin)
            return MakeUnexpected(builtin.error());
        return GpuCommandIrDecodedRecord{ .opcode = __hidden_gpu_command_ir_raster_reader::BuiltinWireOpcode(builtin->opcode), .builtin = *builtin };
    }

    const usize recordOffset = m_cursor;
    GpuCommandIrHeader header;
    FOUNDATION_MEMCPY(&header, sizeof(header), m_commandBytes.data() + recordOffset, sizeof(header));
    if(!GpuCommandIrDetail::IsRasterOpcode(header.opcode)){
        const auto builtin = nextBuiltinTask();
        if(!builtin)
            return MakeUnexpected(builtin.error());
        return GpuCommandIrDecodedRecord{ .opcode = __hidden_gpu_command_ir_raster_reader::BuiltinWireOpcode(builtin->opcode), .builtin = *builtin };
    }

    const usize expectedByteSize = GpuCommandIrDetail::RasterRecordWireByteSize(header.opcode);
    if(header.byteSize != expectedByteSize){
        fail(GpuCommandIrStreamValidationError::InvalidRecordSize, recordOffset, m_nextRecordIndex);
        return MakeUnexpected(GpuCommandIrStreamReadStatus::Error);
    }
    if(expectedByteSize > m_payloadEnd - recordOffset){
        fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
        return MakeUnexpected(GpuCommandIrStreamReadStatus::Error);
    }

    GpuCommandIrDetail::WireRecordStorage wireStorage;
    FOUNDATION_MEMCPY(&wireStorage, sizeof(wireStorage), m_commandBytes.data() + recordOffset, expectedByteSize);
    const auto decodedContext = GpuCommandIrDetail::DecodeRecordContext(wireStorage.endRenderPass.context, m_graphGeneration, m_planGeneration);
    if(!decodedContext){
        fail(GpuCommandIrStreamValidationError::InvalidRecord, recordOffset, m_nextRecordIndex);
        return MakeUnexpected(GpuCommandIrStreamReadStatus::Error);
    }

    GpuCommandIrRasterTaskRecord raster;
    raster.opcode = header.opcode;
    raster.task = decodedContext->task;
    raster.packet = decodedContext->packet;
    raster.queue = decodedContext->queue;
    usize nextBlobOffset = m_nextBlobOffset;
    b8 valid = true;
    switch(header.opcode){
    case GpuCommandIrWireOpcode::SetGraphicsState:{
        const GpuCommandIrSetGraphicsStateRecord& record = wireStorage.setGraphicsState;
        GpuCommandIrStreamValidationError::Enum fieldError = GpuCommandIrStreamValidationError::None;
        if(!GpuCommandIrDetail::ValidateSetGraphicsStateWireFields(record, m_blobBytes, m_nextBlobOffset, fieldError)){
            fail(fieldError, recordOffset, m_nextRecordIndex);
            return MakeUnexpected(GpuCommandIrStreamReadStatus::Error);
        }
        raster.pipeline = { m_graphGeneration, record.pipelineIndex };
        raster.colorAttachment = { m_graphGeneration, record.colorAttachmentIndex };
        raster.framebufferOwnerIndex = record.framebufferOwnerIndex;
        raster.indexOffset = record.indexOffset;
        raster.indexFormat = static_cast<Format::Enum>(record.indexFormat);
        raster.dynamicStencilRefValue = record.stencilRef;
        raster.hasScissor = record.hasScissor != 0u;
        raster.scissor = { record.scissor.minX, record.scissor.maxX, record.scissor.minY, record.scissor.maxY };
        raster.blendConstantColor = { record.blendConstantColor.r, record.blendConstantColor.g, record.blendConstantColor.b, record.blendConstantColor.a };
        raster.viewport = { record.viewportMinX, record.viewportMaxX, record.viewportMinY, record.viewportMaxY, record.viewportMinZ, record.viewportMaxZ };
        raster.blobOffsetBytes = record.blobOffsetBytes;
        raster.blobSizeBytes = record.blobSizeBytes;
        if(record.indexResourceIndex != Limit<u32>::s_Max)
            raster.indexResource = { m_graphGeneration, record.indexResourceIndex };
        if(record.vertexBindingCount != 0u){
            const u8* const vertexPtr = m_blobBytes.data() + static_cast<usize>(record.blobOffsetBytes);
            for(u32 index = 0u; index < record.vertexBindingCount; ++index){
                GpuCommandIrRasterVertexWire binding;
                FOUNDATION_MEMCPY(&binding, sizeof(binding), vertexPtr + index * sizeof(GpuCommandIrRasterVertexWire), sizeof(GpuCommandIrRasterVertexWire));
                raster.vertexBuffers.push_back({ { m_graphGeneration, binding.resourceIndex }, binding.slot, binding.offset });
            }
        }
        nextBlobOffset += static_cast<usize>(record.blobSizeBytes);
        break;
    }
    case GpuCommandIrWireOpcode::BindGraphicsHeap:{
        const GpuCommandIrBindGraphicsHeapRecord& record = wireStorage.bindGraphicsHeap;
        valid = record.pipelineIndex != Limit<u32>::s_Max && record.heapOwnerIndex != Limit<u32>::s_Max;
        raster.pipeline = { m_graphGeneration, record.pipelineIndex };
        raster.heapOwnerIndex = record.heapOwnerIndex;
        break;
    }
    case GpuCommandIrWireOpcode::SetPushConstants:{
        const GpuCommandIrSetPushConstantsRecord& record = wireStorage.setPushConstants;
        valid = record.blobSizeBytes != 0u && record.blobSizeBytes <= Limit<u32>::s_Max && (record.blobSizeBytes & (s_PushConstantSizeAlignment - 1u)) == 0u;
        if(valid && (record.blobOffsetBytes != m_nextBlobOffset || record.blobSizeBytes > m_blobBytes.size() - m_nextBlobOffset)){
            fail(GpuCommandIrStreamValidationError::InvalidBlobRange, recordOffset, m_nextRecordIndex);
            return MakeUnexpected(GpuCommandIrStreamReadStatus::Error);
        }
        raster.blobOffsetBytes = record.blobOffsetBytes;
        raster.blobSizeBytes = record.blobSizeBytes;
        if(valid)
            nextBlobOffset += static_cast<usize>(record.blobSizeBytes);
        break;
    }
    case GpuCommandIrWireOpcode::Draw:
    case GpuCommandIrWireOpcode::DrawIndexed:{
        const GpuCommandIrDrawRecord& record = wireStorage.draw;
        raster.drawArguments = { record.vertexCount, record.instanceCount, record.startIndexLocation, record.startVertexLocation, record.startInstanceLocation };
        break;
    }
    case GpuCommandIrWireOpcode::EndRenderPass:
        break;
    default:
        valid = false;
        break;
    }
    if(!valid){
        fail(GpuCommandIrStreamValidationError::InvalidRecord, recordOffset, m_nextRecordIndex);
        return MakeUnexpected(GpuCommandIrStreamReadStatus::Error);
    }
    m_cursor = recordOffset + expectedByteSize;
    m_nextBlobOffset = nextBlobOffset;
    ++m_nextRecordIndex;
    return GpuCommandIrDecodedRecord{ .opcode = header.opcode, .raster = raster };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

