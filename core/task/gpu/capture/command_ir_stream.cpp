// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir_stream{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool DecodeContext(
    const GpuCommandIrRecordContext& context,
    const u64 graphGeneration,
    const u64 planGeneration,
    GpuCommandIrBuiltinTaskRecord& outRecord
)noexcept{
    outRecord.task = GpuTaskId{ .generation = graphGeneration, .index = context.taskIndex };
    outRecord.packet = GpuSubmissionPacketId{ .generation = planGeneration, .index = context.packetIndex };
    outRecord.queue = GpuPhysicalQueueId{ .index = context.queueIndex, .deviceGeneration = context.queueDeviceGeneration };
    return outRecord.task.valid() && outRecord.packet.valid() && outRecord.queue.valid();
}

[[nodiscard]] static bool DecodeResource(
    const u32 resourceIndex,
    const u64 graphGeneration,
    GpuGraphResourceId& outResource
)noexcept{
    outResource = GpuGraphResourceId{ .generation = graphGeneration, .index = resourceIndex };
    return outResource.valid();
}

[[nodiscard]] static TextureSlice DecodeTextureSlice(const GpuCommandIrTextureSlice& slice)noexcept{
    return TextureSlice{
        .x = slice.x,
        .y = slice.y,
        .z = slice.z,
        .width = slice.width,
        .height = slice.height,
        .depth = slice.depth,
        .mipLevel = slice.mipLevel,
        .arraySlice = slice.arraySlice,
    };
}

[[nodiscard]] static TextureSubresourceSet DecodeSubresources(
    const GpuCommandIrTextureSubresourceSet& subresources
)noexcept{
    return TextureSubresourceSet(
        subresources.baseMipLevel,
        subresources.numMipLevels,
        subresources.baseArraySlice,
        subresources.numArraySlices
    );
}

[[nodiscard]] static Rect DecodeRect(const GpuCommandIrRect& rect)noexcept{
    return Rect(rect.minX, rect.maxX, rect.minY, rect.maxY);
}

[[nodiscard]] static Color DecodeColor(const GpuCommandIrFloatColor& color)noexcept{
    return Color(color.r, color.g, color.b, color.a);
}

[[nodiscard]] static UIntColor DecodeColor(const GpuCommandIrUIntColor& color)noexcept{
    return UIntColor(color.r, color.g, color.b, color.a);
}

[[nodiscard]] static IntColor DecodeColor(const GpuCommandIrIntColor& color)noexcept{
    return IntColor(color.r, color.g, color.b, color.a);
}

[[nodiscard]] static bool ValidateClearTextureRecord(
    const GpuCommandIrClearTextureRecord& record
)noexcept{
    constexpr u8 allowedFlags = static_cast<u8>(
        GpuCommandIrClearTextureFlag::ClearDepth | GpuCommandIrClearTextureFlag::ClearStencil
    );
    const u8 clearFlags = static_cast<u8>(record.clearFlags);
    if(
        record.destinationSubresources.numMipLevels == 0u
        || record.destinationSubresources.numArraySlices == 0u
        || record.clearTextureValueType >= static_cast<u8>(GpuClearTextureTaskValueType::kCount)
        || (clearFlags & ~allowedFlags) != 0u
        || record.reserved != 0u
    )
        return false;

    return record.clearTextureValueType == GpuClearTextureTaskValueType::DepthStencil
        ? clearFlags != GpuCommandIrClearTextureFlag::None
        : clearFlags == GpuCommandIrClearTextureFlag::None
    ;
}

[[nodiscard]] static bool ValidateClearTextureRectUIntRecord(
    const GpuCommandIrClearTextureRectUIntRecord& record
)noexcept{
    return record.destinationSubresources.numMipLevels != 0u
        && record.destinationSubresources.numArraySlices != 0u
        && record.clearRect.maxX > record.clearRect.minX
        && record.clearRect.maxY > record.clearRect.minY
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrStreamReader::GpuCommandIrStreamReader(const BinaryByteView bytes)noexcept
    : m_bytes(bytes)
{
    if(!m_bytes.empty() && !m_bytes.data()){
        fail(GpuCommandIrStreamValidationError::NullData, 0u, Limit<u64>::s_Max);
        return;
    }
    if(m_bytes.size() < sizeof(GpuCommandIrStreamHeaderPrefix)){
        fail(GpuCommandIrStreamValidationError::TruncatedStreamHeader, 0u, Limit<u64>::s_Max);
        return;
    }

    usize cursor = 0u;
    GpuCommandIrStreamHeaderPrefix prefix;
    if(!ReadPOD(m_bytes, cursor, prefix)){
        fail(GpuCommandIrStreamValidationError::TruncatedStreamHeader, 0u, Limit<u64>::s_Max);
        return;
    }
    if(prefix.magic != s_GpuCommandIrStreamMagic){
        fail(GpuCommandIrStreamValidationError::InvalidMagic, 0u, Limit<u64>::s_Max);
        return;
    }
    if(prefix.version != s_GpuCommandIrStreamVersion){
        fail(GpuCommandIrStreamValidationError::UnsupportedVersion, 0u, Limit<u64>::s_Max);
        return;
    }
    if(prefix.reserved != 0u){
        fail(GpuCommandIrStreamValidationError::InvalidHeaderReserved, 0u, Limit<u64>::s_Max);
        return;
    }
    if(m_bytes.size() < sizeof(GpuCommandIrStreamHeader)){
        fail(GpuCommandIrStreamValidationError::TruncatedStreamHeader, 0u, Limit<u64>::s_Max);
        return;
    }

    cursor = 0u;
    GpuCommandIrStreamHeader header;
    if(!ReadPOD(m_bytes, cursor, header)){
        fail(GpuCommandIrStreamValidationError::TruncatedStreamHeader, 0u, Limit<u64>::s_Max);
        return;
    }
    if(
        header.commandBytes > static_cast<u64>(Limit<usize>::s_Max)
        || header.blobBytes > static_cast<u64>(Limit<usize>::s_Max)
    ){
        fail(GpuCommandIrStreamValidationError::PayloadSizeMismatch, 0u, Limit<u64>::s_Max);
        return;
    }

    const usize payloadBytes = m_bytes.size() - sizeof(GpuCommandIrStreamHeader);
    const usize commandBytes = static_cast<usize>(header.commandBytes);
    const usize blobBytes = static_cast<usize>(header.blobBytes);
    if(commandBytes > payloadBytes || blobBytes != payloadBytes - commandBytes){
        fail(GpuCommandIrStreamValidationError::PayloadSizeMismatch, 0u, Limit<u64>::s_Max);
        return;
    }
    if(
        (header.recordCount == 0u && header.graphGeneration != 0u)
        || (header.recordCount != 0u && header.graphGeneration == 0u)
    ){
        fail(GpuCommandIrStreamValidationError::InvalidGraphGeneration, 0u, Limit<u64>::s_Max);
        return;
    }
    if(
        (header.recordCount == 0u && header.planGeneration != 0u)
        || (header.recordCount != 0u && header.planGeneration == 0u)
    ){
        fail(GpuCommandIrStreamValidationError::InvalidPlanGeneration, 0u, Limit<u64>::s_Max);
        return;
    }
    if(header.recordCount > header.commandBytes / sizeof(GpuCommandIrHeader)){
        fail(GpuCommandIrStreamValidationError::InvalidRecordCount, 0u, Limit<u64>::s_Max);
        return;
    }

    m_cursor = cursor;
    m_payloadEnd = sizeof(GpuCommandIrStreamHeader) + commandBytes;
    m_blobBegin = m_payloadEnd;
    m_graphGeneration = header.graphGeneration;
    m_planGeneration = header.planGeneration;
    m_recordCount = header.recordCount;
}

BinaryByteView GpuCommandIrStreamReader::blobBytes()const noexcept{
    if(m_validation.failed() || m_blobBegin == m_bytes.size())
        return {};
    return BinaryByteView{ m_bytes.data() + m_blobBegin, m_bytes.size() - m_blobBegin };
}

GpuCommandIrStreamReadStatus::Enum GpuCommandIrStreamReader::next(
    GpuCommandIrBuiltinTaskRecord& outRecord
)noexcept{
    if(m_validation.failed())
        return GpuCommandIrStreamReadStatus::Error;

    if(m_nextRecordIndex == m_recordCount){
        if(m_cursor != m_payloadEnd){
            fail(
                GpuCommandIrStreamValidationError::TrailingPayload,
                m_cursor,
                m_nextRecordIndex
            );
            return GpuCommandIrStreamReadStatus::Error;
        }
        if(m_nextBlobOffset != m_bytes.size() - m_blobBegin){
            fail(GpuCommandIrStreamValidationError::InvalidBlobRange, m_blobBegin, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        m_validation.byteOffset = m_payloadEnd;
        m_validation.recordIndex = m_nextRecordIndex;
        m_validation.complete = true;
        return GpuCommandIrStreamReadStatus::End;
    }

    const usize recordOffset = m_cursor;
    if(m_payloadEnd - recordOffset < sizeof(GpuCommandIrHeader)){
        fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }
    usize recordCursor = recordOffset;
    GpuCommandIrHeader header;
    if(!ReadPOD(m_bytes, recordCursor, header)){
        fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }
    if(header.byteSize < sizeof(GpuCommandIrHeader)){
        fail(GpuCommandIrStreamValidationError::InvalidRecordSize, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }

    usize expectedByteSize = 0u;
    switch(header.opcode){
    case GpuCommandIrWireOpcode::CopyBuffer:
        expectedByteSize = sizeof(GpuCommandIrCopyBufferRecord);
        break;
    case GpuCommandIrWireOpcode::CopyTexture:
        expectedByteSize = sizeof(GpuCommandIrCopyTextureRecord);
        break;
    case GpuCommandIrWireOpcode::ClearBuffer:
        expectedByteSize = sizeof(GpuCommandIrClearBufferRecord);
        break;
    case GpuCommandIrWireOpcode::ClearTexture:
        expectedByteSize = sizeof(GpuCommandIrClearTextureRecord);
        break;
    case GpuCommandIrWireOpcode::ClearTextureRectUInt:
        expectedByteSize = sizeof(GpuCommandIrClearTextureRectUIntRecord);
        break;
    case GpuCommandIrWireOpcode::UploadBuffer:
        expectedByteSize = sizeof(GpuCommandIrUploadBufferRecord);
        break;
    case GpuCommandIrWireOpcode::UploadTexture:
        expectedByteSize = sizeof(GpuCommandIrUploadTextureRecord);
        break;
    default:
        fail(GpuCommandIrStreamValidationError::UnsupportedOpcode, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }
    if(header.byteSize != expectedByteSize){
        fail(GpuCommandIrStreamValidationError::InvalidRecordSize, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }
    if(expectedByteSize > m_payloadEnd - recordOffset){
        fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }

    GpuCommandIrBuiltinTaskRecord decoded;
    bool decodedRecord = false;
    usize nextBlobOffset = m_nextBlobOffset;
    using namespace __hidden_gpu_command_ir_stream;
    switch(header.opcode){
    case GpuCommandIrWireOpcode::CopyBuffer:{
        GpuCommandIrCopyBufferRecord record;
        recordCursor = recordOffset;
        if(!ReadPOD(m_bytes, recordCursor, record)){
            fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        decoded.opcode = GpuCommandIrOpcode::CopyBuffer;
        decodedRecord = DecodeContext(record.context, m_graphGeneration, m_planGeneration, decoded)
            && DecodeResource(record.sourceResourceIndex, m_graphGeneration, decoded.source)
            && DecodeResource(record.destinationResourceIndex, m_graphGeneration, decoded.destination)
            && record.dataSizeBytes != 0u
        ;
        decoded.sourceOffsetBytes = record.sourceOffsetBytes;
        decoded.destinationOffsetBytes = record.destinationOffsetBytes;
        decoded.dataSizeBytes = record.dataSizeBytes;
        break;
    }
    case GpuCommandIrWireOpcode::CopyTexture:{
        GpuCommandIrCopyTextureRecord record;
        recordCursor = recordOffset;
        if(!ReadPOD(m_bytes, recordCursor, record)){
            fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        decoded.opcode = GpuCommandIrOpcode::CopyTexture;
        decodedRecord = DecodeContext(record.context, m_graphGeneration, m_planGeneration, decoded)
            && DecodeResource(record.sourceResourceIndex, m_graphGeneration, decoded.source)
            && DecodeResource(record.destinationResourceIndex, m_graphGeneration, decoded.destination)
        ;
        decoded.sourceSlice = DecodeTextureSlice(record.sourceSlice);
        decoded.destinationSlice = DecodeTextureSlice(record.destinationSlice);
        break;
    }
    case GpuCommandIrWireOpcode::ClearBuffer:{
        GpuCommandIrClearBufferRecord record;
        recordCursor = recordOffset;
        if(!ReadPOD(m_bytes, recordCursor, record)){
            fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        decoded.opcode = GpuCommandIrOpcode::ClearBuffer;
        decodedRecord = DecodeContext(record.context, m_graphGeneration, m_planGeneration, decoded)
            && DecodeResource(record.destinationResourceIndex, m_graphGeneration, decoded.destination)
        ;
        decoded.uintClearValue = UIntColor(record.clearValue);
        break;
    }
    case GpuCommandIrWireOpcode::ClearTexture:{
        GpuCommandIrClearTextureRecord record;
        recordCursor = recordOffset;
        if(!ReadPOD(m_bytes, recordCursor, record)){
            fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        decoded.opcode = GpuCommandIrOpcode::ClearTexture;
        decodedRecord = DecodeContext(record.context, m_graphGeneration, m_planGeneration, decoded)
            && DecodeResource(record.destinationResourceIndex, m_graphGeneration, decoded.destination)
            && ValidateClearTextureRecord(record)
        ;
        decoded.destinationSubresources = DecodeSubresources(record.destinationSubresources);
        decoded.clearTextureValueType = static_cast<GpuClearTextureTaskValueType::Enum>(
            record.clearTextureValueType
        );
        decoded.floatClearValue = DecodeColor(record.floatClearValue);
        decoded.uintClearValue = DecodeColor(record.uintClearValue);
        decoded.intClearValue = DecodeColor(record.intClearValue);
        decoded.depthClearValue = record.depthClearValue;
        decoded.stencilClearValue = record.stencilClearValue;
        decoded.clearDepth = (static_cast<u8>(record.clearFlags) & GpuCommandIrClearTextureFlag::ClearDepth) != 0u;
        decoded.clearStencil = (static_cast<u8>(record.clearFlags) & GpuCommandIrClearTextureFlag::ClearStencil) != 0u;
        break;
    }
    case GpuCommandIrWireOpcode::ClearTextureRectUInt:{
        GpuCommandIrClearTextureRectUIntRecord record;
        recordCursor = recordOffset;
        if(!ReadPOD(m_bytes, recordCursor, record)){
            fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        decoded.opcode = GpuCommandIrOpcode::ClearTextureRectUInt;
        decodedRecord = DecodeContext(record.context, m_graphGeneration, m_planGeneration, decoded)
            && DecodeResource(record.destinationResourceIndex, m_graphGeneration, decoded.destination)
            && ValidateClearTextureRectUIntRecord(record)
        ;
        decoded.destinationSubresources = DecodeSubresources(record.destinationSubresources);
        decoded.clearRect = DecodeRect(record.clearRect);
        decoded.uintClearValue = DecodeColor(record.uintClearValue);
        break;
    }
    case GpuCommandIrWireOpcode::UploadBuffer:{
        GpuCommandIrUploadBufferRecord record;
        recordCursor = recordOffset;
        if(!ReadPOD(m_bytes, recordCursor, record)){
            fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        decoded.opcode = GpuCommandIrOpcode::UploadBuffer;
        decodedRecord = DecodeContext(record.context, m_graphGeneration, m_planGeneration, decoded)
            && DecodeResource(record.destinationResourceIndex, m_graphGeneration, decoded.destination)
            && record.sourceUploadBlobIndex != Limit<u32>::s_Max
            && record.reserved == 0u
        ;
        decoded.sourceUploadBlob = GpuUploadBlobId{
            .generation = m_graphGeneration,
            .index = record.sourceUploadBlobIndex,
        };
        decoded.blobOffsetBytes = record.blobOffsetBytes;
        decoded.blobSizeBytes = record.blobSizeBytes;
        decoded.destinationOffsetBytes = record.destinationOffsetBytes;
        decoded.finalState = static_cast<ResourceStates::Mask>(record.finalState);
        if(
            decodedRecord
            && (
                record.blobSizeBytes == 0u
                || record.blobOffsetBytes != m_nextBlobOffset
                || record.blobSizeBytes > m_bytes.size() - m_blobBegin - m_nextBlobOffset
            )
        ){
            fail(GpuCommandIrStreamValidationError::InvalidBlobRange, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        if(decodedRecord)
            nextBlobOffset += static_cast<usize>(record.blobSizeBytes);
        break;
    }
    case GpuCommandIrWireOpcode::UploadTexture:{
        GpuCommandIrUploadTextureRecord record;
        recordCursor = recordOffset;
        if(!ReadPOD(m_bytes, recordCursor, record)){
            fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        decoded.opcode = GpuCommandIrOpcode::UploadTexture;
        decodedRecord = DecodeContext(record.context, m_graphGeneration, m_planGeneration, decoded)
            && DecodeResource(record.destinationResourceIndex, m_graphGeneration, decoded.destination)
            && record.sourceUploadBlobIndex != Limit<u32>::s_Max
            && record.aspect < TextureUploadAspect::kCount
            && record.reserved0 == 0u
            && record.reserved1 == 0u
        ;
        decoded.sourceUploadBlob = GpuUploadBlobId{
            .generation = m_graphGeneration,
            .index = record.sourceUploadBlobIndex,
        };
        decoded.blobOffsetBytes = record.blobOffsetBytes;
        decoded.blobSizeBytes = record.blobSizeBytes;
        decoded.destinationSlice = DecodeTextureSlice(record.destinationSlice);
        decoded.rowPitch = record.rowPitch;
        decoded.depthPitch = record.depthPitch;
        decoded.finalState = static_cast<ResourceStates::Mask>(record.finalState);
        decoded.uploadAspect = static_cast<TextureUploadAspect::Enum>(record.aspect);
        if(
            decodedRecord
            && (
                record.blobSizeBytes == 0u
                || record.blobOffsetBytes != m_nextBlobOffset
                || record.blobSizeBytes > m_bytes.size() - m_blobBegin - m_nextBlobOffset
            )
        ){
            fail(GpuCommandIrStreamValidationError::InvalidBlobRange, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        if(decodedRecord)
            nextBlobOffset += static_cast<usize>(record.blobSizeBytes);
        break;
    }
    default:
        NWB_ASSERT_MSG(false, NWB_TEXT("Known command IR opcode lost its decoder"));
        fail(GpuCommandIrStreamValidationError::UnsupportedOpcode, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }

    if(!decodedRecord || !GpuCommandIrDetail::ValidateBuiltinRecord(decoded)){
        fail(GpuCommandIrStreamValidationError::InvalidRecord, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }

    m_cursor = recordCursor;
    m_nextBlobOffset = nextBlobOffset;
    ++m_nextRecordIndex;
    outRecord = decoded;
    return GpuCommandIrStreamReadStatus::Record;
}

void GpuCommandIrStreamReader::fail(
    const GpuCommandIrStreamValidationError::Enum error,
    const usize byteOffset,
    const u64 recordIndex
)noexcept{
    if(m_validation.failed())
        return;

    m_validation.error = error;
    m_validation.byteOffset = byteOffset;
    m_validation.recordIndex = recordIndex;
    m_validation.complete = true;
}

GpuCommandIrStreamValidationResult ValidateGpuCommandIrStream(const BinaryByteView bytes)noexcept{
    GpuCommandIrStreamReader reader(bytes);
    GpuCommandIrBuiltinTaskRecord record;
    for(;;){
        switch(reader.next(record)){
        case GpuCommandIrStreamReadStatus::Record:
            break;
        case GpuCommandIrStreamReadStatus::End:
        case GpuCommandIrStreamReadStatus::Error:
            return reader.validation();
        default:
            NWB_ASSERT_MSG(false, NWB_TEXT("Unknown command IR reader status"));
            return reader.validation();
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

