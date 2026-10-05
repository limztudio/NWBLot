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

[[nodiscard]] static bool DecodeContext(
    const GpuCommandIrRecordContext& context,
    const u64 graphGeneration,
    const u64 planGeneration,
    GpuCommandIrRasterTaskRecord& outRecord
)noexcept{
    if(
        context.taskIndex == Limit<u32>::s_Max
        || context.packetIndex == Limit<u32>::s_Max
        || context.queueIndex == Limit<u16>::s_Max
        || context.queueDeviceGeneration == 0u
    )
        return false;
    outRecord.task = { graphGeneration, context.taskIndex };
    outRecord.packet = { planGeneration, context.packetIndex };
    outRecord.queue = { context.queueIndex, context.queueDeviceGeneration };
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrStreamReadStatus::Enum GpuCommandIrStreamReader::next(GpuCommandIrDecodedRecord& outRecord)noexcept{
    if(m_validation.failed())
        return GpuCommandIrStreamReadStatus::Error;
    if(m_nextRecordIndex == m_recordCount){
        GpuCommandIrBuiltinTaskRecord ignored;
        return next(ignored);
    }
    if(m_payloadEnd - m_cursor < sizeof(GpuCommandIrHeader)){
        GpuCommandIrBuiltinTaskRecord ignored;
        return next(ignored);
    }

    const usize recordOffset = m_cursor;
    usize probeCursor = m_cursor;
    GpuCommandIrHeader header;
    if(!ReadPOD(m_bytes, probeCursor, header)){
        fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }
    if(!GpuCommandIrDetail::IsRasterOpcode(header.opcode)){
        GpuCommandIrBuiltinTaskRecord builtin;
        const GpuCommandIrStreamReadStatus::Enum status = next(builtin);
        if(status == GpuCommandIrStreamReadStatus::Record){
            outRecord = {};
            outRecord.opcode = __hidden_gpu_command_ir_raster_reader::BuiltinWireOpcode(builtin.opcode);
            outRecord.builtin = builtin;
        }
        return status;
    }

    usize expectedByteSize = 0u;
    switch(header.opcode){
    case GpuCommandIrWireOpcode::SetGraphicsState: expectedByteSize = sizeof(GpuCommandIrSetGraphicsStateRecord); break;
    case GpuCommandIrWireOpcode::BindGraphicsHeap: expectedByteSize = sizeof(GpuCommandIrBindGraphicsHeapRecord); break;
    case GpuCommandIrWireOpcode::SetPushConstants: expectedByteSize = sizeof(GpuCommandIrSetPushConstantsRecord); break;
    case GpuCommandIrWireOpcode::Draw:
    case GpuCommandIrWireOpcode::DrawIndexed: expectedByteSize = sizeof(GpuCommandIrDrawRecord); break;
    case GpuCommandIrWireOpcode::EndRenderPass: expectedByteSize = sizeof(GpuCommandIrEndRenderPassRecord); break;
    default: break;
    }
    if(header.byteSize != expectedByteSize){
        fail(GpuCommandIrStreamValidationError::InvalidRecordSize, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }
    if(expectedByteSize > m_payloadEnd - recordOffset){
        fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }

    GpuCommandIrRasterTaskRecord raster;
    raster.opcode = header.opcode;
    GpuCommandIrRecordContext context;
    usize contextCursor = recordOffset + sizeof(GpuCommandIrHeader);
    if(!ReadPOD(m_bytes, contextCursor, context)
        || !__hidden_gpu_command_ir_raster_reader::DecodeContext(context, m_graphGeneration, m_planGeneration, raster)){
        fail(GpuCommandIrStreamValidationError::InvalidRecord, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }
    usize nextBlobOffset = m_nextBlobOffset;
    bool valid = true;
    switch(header.opcode){
    case GpuCommandIrWireOpcode::SetGraphicsState:{
        GpuCommandIrSetGraphicsStateRecord record;
        usize cursor = recordOffset;
        valid = ReadPOD(m_bytes, cursor, record)
            && record.pipelineIndex != Limit<u32>::s_Max
            && record.colorAttachmentIndex != Limit<u32>::s_Max
            && record.framebufferOwnerIndex != Limit<u32>::s_Max
            && record.vertexBindingCount <= s_MaxVertexAttributes
            && record.hasScissor <= 1u
        ;
        if(!valid)
            break;
        if(
            record.blobSizeBytes != static_cast<u64>(record.vertexBindingCount) * sizeof(GpuCommandIrRasterVertexWire)
            || record.blobOffsetBytes != m_nextBlobOffset
            || record.blobSizeBytes > m_bytes.size() - m_blobBegin - m_nextBlobOffset
        ){
            fail(GpuCommandIrStreamValidationError::InvalidBlobRange, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        raster.pipeline = { m_graphGeneration, record.pipelineIndex };
        raster.colorAttachment = { m_graphGeneration, record.colorAttachmentIndex };
        raster.framebufferOwnerIndex = record.framebufferOwnerIndex;
        raster.indexOffset = record.indexOffset;
        raster.indexFormat = static_cast<Format::Enum>(record.indexFormat);
        raster.dynamicStencilRefValue = record.stencilRef;
        raster.hasScissor = record.hasScissor != 0u;
        raster.scissor = { record.scissor.minX, record.scissor.maxX, record.scissor.minY, record.scissor.maxY };
        raster.blendConstantColor = { record.blendConstantColor.r, record.blendConstantColor.g,
            record.blendConstantColor.b, record.blendConstantColor.a };
        raster.viewport = { record.viewportMinX, record.viewportMaxX, record.viewportMinY,
            record.viewportMaxY, record.viewportMinZ, record.viewportMaxZ };
        raster.blobOffsetBytes = record.blobOffsetBytes;
        raster.blobSizeBytes = record.blobSizeBytes;
        if(record.indexResourceIndex != Limit<u32>::s_Max)
            raster.indexResource = { m_graphGeneration, record.indexResourceIndex };
        valid = GpuCommandIrDetail::IsRasterViewportValid(raster.viewport)
            && IsFinite(raster.blendConstantColor.r) && IsFinite(raster.blendConstantColor.g)
            && IsFinite(raster.blendConstantColor.b) && IsFinite(raster.blendConstantColor.a)
            && (!raster.hasScissor || (raster.scissor.minX < raster.scissor.maxX && raster.scissor.minY < raster.scissor.maxY))
            && (raster.hasScissor || (record.scissor.minX == 0 && record.scissor.maxX == 0
                && record.scissor.minY == 0 && record.scissor.maxY == 0))
            && (raster.indexResource.valid()
                ? (raster.indexFormat == Format::R16_UINT || raster.indexFormat == Format::R32_UINT)
                : (raster.indexFormat == Format::UNKNOWN && raster.indexOffset == 0u))
        ;
        if(!valid)
            break;
        usize vertexCursor = m_blobBegin + static_cast<usize>(record.blobOffsetBytes);
        for(u32 index = 0u; index < record.vertexBindingCount; ++index){
            GpuCommandIrRasterVertexWire binding;
            if(!ReadPOD(m_bytes, vertexCursor, binding) || binding.resourceIndex == Limit<u32>::s_Max){
                valid = false;
                break;
            }
            for(const GpuCommandIrRasterVertexBinding& previous : raster.vertexBuffers){
                if(previous.slot == binding.slot){
                    valid = false;
                    break;
                }
            }
            if(!valid)
                break;
            raster.vertexBuffers.push_back({ { m_graphGeneration, binding.resourceIndex }, binding.slot, binding.offset });
        }
        nextBlobOffset += static_cast<usize>(record.blobSizeBytes);
        break;
    }
    case GpuCommandIrWireOpcode::BindGraphicsHeap:{
        GpuCommandIrBindGraphicsHeapRecord record;
        usize cursor = recordOffset;
        valid = ReadPOD(m_bytes, cursor, record)
            && record.pipelineIndex != Limit<u32>::s_Max
            && record.heapOwnerIndex != Limit<u32>::s_Max;
        raster.pipeline = { m_graphGeneration, record.pipelineIndex };
        raster.heapOwnerIndex = record.heapOwnerIndex;
        break;
    }
    case GpuCommandIrWireOpcode::SetPushConstants:{
        GpuCommandIrSetPushConstantsRecord record;
        usize cursor = recordOffset;
        valid = ReadPOD(m_bytes, cursor, record)
            && record.blobSizeBytes != 0u
            && record.blobSizeBytes <= Limit<u32>::s_Max
            && (record.blobSizeBytes & 3u) == 0u;
        if(valid && (
            record.blobOffsetBytes != m_nextBlobOffset
            || record.blobSizeBytes > m_bytes.size() - m_blobBegin - m_nextBlobOffset
        )){
            fail(GpuCommandIrStreamValidationError::InvalidBlobRange, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        raster.blobOffsetBytes = record.blobOffsetBytes;
        raster.blobSizeBytes = record.blobSizeBytes;
        if(valid)
            nextBlobOffset += static_cast<usize>(record.blobSizeBytes);
        break;
    }
    case GpuCommandIrWireOpcode::Draw:
    case GpuCommandIrWireOpcode::DrawIndexed:{
        GpuCommandIrDrawRecord record;
        usize cursor = recordOffset;
        valid = ReadPOD(m_bytes, cursor, record);
        raster.drawArguments = { record.vertexCount, record.instanceCount, record.startIndexLocation,
            record.startVertexLocation, record.startInstanceLocation };
        break;
    }
    case GpuCommandIrWireOpcode::EndRenderPass:{
        GpuCommandIrEndRenderPassRecord record;
        usize cursor = recordOffset;
        valid = ReadPOD(m_bytes, cursor, record);
        break;
    }
    default:
        valid = false;
        break;
    }
    if(!valid){
        fail(GpuCommandIrStreamValidationError::InvalidRecord, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }
    m_cursor = recordOffset + expectedByteSize;
    m_nextBlobOffset = nextBlobOffset;
    ++m_nextRecordIndex;
    outRecord = {};
    outRecord.opcode = header.opcode;
    outRecord.raster = raster;
    return GpuCommandIrStreamReadStatus::Record;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

