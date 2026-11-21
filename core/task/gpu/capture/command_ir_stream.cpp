// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir_stream{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<GpuGraphResourceId> DecodeResource(const u32 resourceIndex, const u64 graphGeneration)noexcept{
    const GpuGraphResourceId resource{ .generation = graphGeneration, .index = resourceIndex };
    if(!resource.valid())
        return MakeUnexpected(Failure{});
    return resource;
}

[[nodiscard]] static TextureSlice DecodeTextureSlice(const GpuCommandIrTextureSlice& slice)noexcept{
    return TextureSlice{ .x = slice.x, .y = slice.y, .z = slice.z, .width = slice.width, .height = slice.height, .depth = slice.depth, .mipLevel = slice.mipLevel, .arraySlice = slice.arraySlice, };
}

[[nodiscard]] static TextureSubresourceSet DecodeSubresources(const GpuCommandIrTextureSubresourceSet& subresources)noexcept{
    return TextureSubresourceSet(subresources.baseMipLevel, subresources.numMipLevels, subresources.baseArraySlice, subresources.numArraySlices);
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

[[nodiscard]] static b8 ValidateClearTextureRecord(const GpuCommandIrClearTextureRecord& record)noexcept{
    constexpr u8 s_AllowedFlags = static_cast<u8>(GpuCommandIrClearTextureFlag::ClearDepth | GpuCommandIrClearTextureFlag::ClearStencil);
    const u8 clearFlags = static_cast<u8>(record.clearFlags);
    if(
        record.destinationSubresources.numMipLevels == 0u
        || record.destinationSubresources.numArraySlices == 0u
        || record.clearTextureValueType >= static_cast<u8>(GpuClearTextureTaskValueType::Count)
        || (clearFlags & ~s_AllowedFlags) != 0u
        || record.reserved != 0u
    )
        return false;

    return record.clearTextureValueType == GpuClearTextureTaskValueType::DepthStencil ? clearFlags != GpuCommandIrClearTextureFlag::None : clearFlags == GpuCommandIrClearTextureFlag::None;
}

[[nodiscard]] static b8 ValidateClearTextureRectUIntRecord(const GpuCommandIrClearTextureRectUIntRecord& record)noexcept{
    return record.destinationSubresources.numMipLevels != 0u && record.destinationSubresources.numArraySlices != 0u && record.clearRect.maxX > record.clearRect.minX && record.clearRect.maxY > record.clearRect.minY;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrStreamReader::GpuCommandIrStreamReader(const BinaryByteView bytes)noexcept{
    GpuCommandIrStreamHeader header;
    if(!initializeValidatedHeader(bytes, header))
        return;

    const usize payloadBytes = bytes.size() - sizeof(GpuCommandIrStreamHeader);
    const usize commandBytes = static_cast<usize>(header.commandBytes);
    const usize blobBytes = static_cast<usize>(header.blobBytes);
    if(commandBytes > payloadBytes || blobBytes != payloadBytes - commandBytes){
        fail(GpuCommandIrStreamValidationError::PayloadSizeMismatch, 0u, Limit<u64>::s_Max);
        return;
    }

    const BinaryByteView commandView{ bytes.data(), sizeof(GpuCommandIrStreamHeader) + commandBytes };
    const BinaryByteView blobView = blobBytes != 0u
        ? BinaryByteView{ bytes.data() + sizeof(GpuCommandIrStreamHeader) + commandBytes, blobBytes }
        : BinaryByteView{}
    ;
    finalizeValidatedHeader(header, commandView, blobView);
}

GpuCommandIrStreamReader::GpuCommandIrStreamReader(const BinaryByteView commandBytes, const BinaryByteView blobBytes)noexcept{
    if(!blobBytes.empty() && !blobBytes.data()){
        fail(GpuCommandIrStreamValidationError::NullData, 0u, Limit<u64>::s_Max);
        return;
    }

    GpuCommandIrStreamHeader header;
    if(!initializeValidatedHeader(commandBytes, header))
        return;

    if(
        header.commandBytes != static_cast<u64>(commandBytes.size() - sizeof(GpuCommandIrStreamHeader))
        || header.blobBytes != static_cast<u64>(blobBytes.size())
    ){
        fail(GpuCommandIrStreamValidationError::PayloadSizeMismatch, 0u, Limit<u64>::s_Max);
        return;
    }

    finalizeValidatedHeader(header, commandBytes, blobBytes);
}

b8 GpuCommandIrStreamReader::initializeValidatedHeader(const BinaryByteView headerSource, GpuCommandIrStreamHeader& outHeader)noexcept{
    return GpuCommandIrDetail::InitializeValidatedStreamHeader(headerSource, outHeader, m_validation);
}

void GpuCommandIrStreamReader::finalizeValidatedHeader(const GpuCommandIrStreamHeader& header, const BinaryByteView commandBytes, const BinaryByteView blobBytes)noexcept{
    if((header.recordCount == 0u && header.graphGeneration != 0u) || (header.recordCount != 0u && header.graphGeneration == 0u)){
        fail(GpuCommandIrStreamValidationError::InvalidGraphGeneration, 0u, Limit<u64>::s_Max);
        return;
    }
    if((header.recordCount == 0u && header.planGeneration != 0u) || (header.recordCount != 0u && header.planGeneration == 0u)){
        fail(GpuCommandIrStreamValidationError::InvalidPlanGeneration, 0u, Limit<u64>::s_Max);
        return;
    }
    if(header.recordCount > header.commandBytes / sizeof(GpuCommandIrHeader)){
        fail(GpuCommandIrStreamValidationError::InvalidRecordCount, 0u, Limit<u64>::s_Max);
        return;
    }

    m_commandBytes = commandBytes;
    m_blobBytes = blobBytes;
    m_cursor = sizeof(GpuCommandIrStreamHeader);
    m_payloadEnd = commandBytes.size();
    m_graphGeneration = header.graphGeneration;
    m_planGeneration = header.planGeneration;
    m_recordCount = header.recordCount;
}

namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 DecodeAndValidateBuiltinWireRecord(
    const GpuCommandIrWireOpcode::Enum opcode,
    const u8* const recordBytes,
    const BinaryByteView blobBytes,
    const u64 graphGeneration,
    const u64 planGeneration,
    const usize recordOffset,
    const u64 recordIndex,
    usize& inOutNextBlobOffset,
    GpuCommandIrStreamValidationResult& outValidation,
    GpuCommandIrBuiltinTaskRecord& outDecoded
)noexcept{
    using namespace __hidden_gpu_command_ir_stream;

    const auto fail = [&](const GpuCommandIrStreamValidationError::Enum error)noexcept{
        if(!outValidation.failed()){
            outValidation.error = error;
            outValidation.byteOffset = recordOffset;
            outValidation.recordIndex = recordIndex;
            outValidation.complete = true;
        }
        return false;
    };

    outDecoded = {};
    b8 decodedRecord = false;
    usize nextBlobOffset = inOutNextBlobOffset;
    switch(opcode){
    case GpuCommandIrWireOpcode::CopyBuffer:{
        GpuCommandIrCopyBufferRecord record;
        FOUNDATION_MEMCPY(&record, sizeof(record), recordBytes, sizeof(record));
        outDecoded.opcode = GpuCommandIrOpcode::CopyBuffer;
        const auto sourceResource = DecodeResource(record.sourceResourceIndex, graphGeneration);
        const auto destinationResource = DecodeResource(record.destinationResourceIndex, graphGeneration);
        const auto context = DecodeRecordContext(record.context, graphGeneration, planGeneration);
        if(context){
            outDecoded.task = context->task;
            outDecoded.packet = context->packet;
            outDecoded.queue = context->queue;
        }
        decodedRecord = context.has_value() && sourceResource.has_value() && destinationResource.has_value() && record.dataSizeBytes != 0u;
        if(sourceResource)
            outDecoded.source = *sourceResource;
        if(destinationResource)
            outDecoded.destination = *destinationResource;
        outDecoded.sourceOffsetBytes = record.sourceOffsetBytes;
        outDecoded.destinationOffsetBytes = record.destinationOffsetBytes;
        outDecoded.dataSizeBytes = record.dataSizeBytes;
        break;
    }
    case GpuCommandIrWireOpcode::CopyTexture:{
        GpuCommandIrCopyTextureRecord record;
        FOUNDATION_MEMCPY(&record, sizeof(record), recordBytes, sizeof(record));
        outDecoded.opcode = GpuCommandIrOpcode::CopyTexture;
        const auto sourceResource = DecodeResource(record.sourceResourceIndex, graphGeneration);
        const auto destinationResource = DecodeResource(record.destinationResourceIndex, graphGeneration);
        const auto context = DecodeRecordContext(record.context, graphGeneration, planGeneration);
        if(context){
            outDecoded.task = context->task;
            outDecoded.packet = context->packet;
            outDecoded.queue = context->queue;
        }
        decodedRecord = context.has_value() && sourceResource.has_value() && destinationResource.has_value();
        if(sourceResource)
            outDecoded.source = *sourceResource;
        if(destinationResource)
            outDecoded.destination = *destinationResource;
        outDecoded.sourceSlice = DecodeTextureSlice(record.sourceSlice);
        outDecoded.destinationSlice = DecodeTextureSlice(record.destinationSlice);
        break;
    }
    case GpuCommandIrWireOpcode::ClearBuffer:{
        GpuCommandIrClearBufferRecord record;
        FOUNDATION_MEMCPY(&record, sizeof(record), recordBytes, sizeof(record));
        outDecoded.opcode = GpuCommandIrOpcode::ClearBuffer;
        const auto destinationResource = DecodeResource(record.destinationResourceIndex, graphGeneration);
        const auto context = DecodeRecordContext(record.context, graphGeneration, planGeneration);
        if(context){
            outDecoded.task = context->task;
            outDecoded.packet = context->packet;
            outDecoded.queue = context->queue;
        }
        decodedRecord = context.has_value() && destinationResource.has_value();
        if(destinationResource)
            outDecoded.destination = *destinationResource;
        outDecoded.uintClearValue = UIntColor(record.clearValue);
        break;
    }
    case GpuCommandIrWireOpcode::ClearTexture:{
        GpuCommandIrClearTextureRecord record;
        FOUNDATION_MEMCPY(&record, sizeof(record), recordBytes, sizeof(record));
        outDecoded.opcode = GpuCommandIrOpcode::ClearTexture;
        const auto destinationResource = DecodeResource(record.destinationResourceIndex, graphGeneration);
        const auto context = DecodeRecordContext(record.context, graphGeneration, planGeneration);
        if(context){
            outDecoded.task = context->task;
            outDecoded.packet = context->packet;
            outDecoded.queue = context->queue;
        }
        decodedRecord = context.has_value() && destinationResource.has_value() && ValidateClearTextureRecord(record);
        if(destinationResource)
            outDecoded.destination = *destinationResource;
        outDecoded.destinationSubresources = DecodeSubresources(record.destinationSubresources);
        outDecoded.clearTextureValueType = static_cast<GpuClearTextureTaskValueType::Enum>(record.clearTextureValueType);
        outDecoded.floatClearValue = DecodeColor(record.floatClearValue);
        outDecoded.uintClearValue = DecodeColor(record.uintClearValue);
        outDecoded.intClearValue = DecodeColor(record.intClearValue);
        outDecoded.depthClearValue = record.depthClearValue;
        outDecoded.stencilClearValue = record.stencilClearValue;
        outDecoded.clearDepth = (static_cast<u8>(record.clearFlags) & GpuCommandIrClearTextureFlag::ClearDepth) != 0u;
        outDecoded.clearStencil = (static_cast<u8>(record.clearFlags) & GpuCommandIrClearTextureFlag::ClearStencil) != 0u;
        break;
    }
    case GpuCommandIrWireOpcode::ClearTextureRectUInt:{
        GpuCommandIrClearTextureRectUIntRecord record;
        FOUNDATION_MEMCPY(&record, sizeof(record), recordBytes, sizeof(record));
        outDecoded.opcode = GpuCommandIrOpcode::ClearTextureRectUInt;
        const auto destinationResource = DecodeResource(record.destinationResourceIndex, graphGeneration);
        const auto context = DecodeRecordContext(record.context, graphGeneration, planGeneration);
        if(context){
            outDecoded.task = context->task;
            outDecoded.packet = context->packet;
            outDecoded.queue = context->queue;
        }
        decodedRecord = context.has_value() && destinationResource.has_value() && ValidateClearTextureRectUIntRecord(record);
        if(destinationResource)
            outDecoded.destination = *destinationResource;
        outDecoded.destinationSubresources = DecodeSubresources(record.destinationSubresources);
        outDecoded.clearRect = DecodeRect(record.clearRect);
        outDecoded.uintClearValue = DecodeColor(record.uintClearValue);
        break;
    }
    case GpuCommandIrWireOpcode::UploadBuffer:{
        GpuCommandIrUploadBufferRecord record;
        FOUNDATION_MEMCPY(&record, sizeof(record), recordBytes, sizeof(record));
        outDecoded.opcode = GpuCommandIrOpcode::UploadBuffer;
        const auto destinationResource = DecodeResource(record.destinationResourceIndex, graphGeneration);
        const auto context = DecodeRecordContext(record.context, graphGeneration, planGeneration);
        if(context){
            outDecoded.task = context->task;
            outDecoded.packet = context->packet;
            outDecoded.queue = context->queue;
        }
        decodedRecord = context.has_value() && destinationResource.has_value() && record.sourceUploadBlobIndex != Limit<u32>::s_Max && record.reserved == 0u;
        if(destinationResource)
            outDecoded.destination = *destinationResource;
        outDecoded.sourceUploadBlob = GpuUploadBlobId{ .generation = graphGeneration, .index = record.sourceUploadBlobIndex, };
        outDecoded.blobOffsetBytes = record.blobOffsetBytes;
        outDecoded.blobSizeBytes = record.blobSizeBytes;
        outDecoded.destinationOffsetBytes = record.destinationOffsetBytes;
        outDecoded.finalState = static_cast<ResourceStates::Mask>(record.finalState);
        if(decodedRecord && (record.blobSizeBytes == 0u || record.blobOffsetBytes != inOutNextBlobOffset || record.blobSizeBytes > blobBytes.size() - inOutNextBlobOffset))
            return fail(GpuCommandIrStreamValidationError::InvalidBlobRange);
        if(decodedRecord)
            nextBlobOffset += static_cast<usize>(record.blobSizeBytes);
        break;
    }
    case GpuCommandIrWireOpcode::UploadTexture:{
        GpuCommandIrUploadTextureRecord record;
        FOUNDATION_MEMCPY(&record, sizeof(record), recordBytes, sizeof(record));
        outDecoded.opcode = GpuCommandIrOpcode::UploadTexture;
        const auto destinationResource = DecodeResource(record.destinationResourceIndex, graphGeneration);
        const auto context = DecodeRecordContext(record.context, graphGeneration, planGeneration);
        if(context){
            outDecoded.task = context->task;
            outDecoded.packet = context->packet;
            outDecoded.queue = context->queue;
        }
        decodedRecord = context.has_value() && destinationResource.has_value() && record.sourceUploadBlobIndex != Limit<u32>::s_Max && record.aspect < TextureUploadAspect::Count && record.reserved0 == 0u && record.reserved1 == 0u;
        if(destinationResource)
            outDecoded.destination = *destinationResource;
        outDecoded.sourceUploadBlob = GpuUploadBlobId{ .generation = graphGeneration, .index = record.sourceUploadBlobIndex, };
        outDecoded.blobOffsetBytes = record.blobOffsetBytes;
        outDecoded.blobSizeBytes = record.blobSizeBytes;
        outDecoded.destinationSlice = DecodeTextureSlice(record.destinationSlice);
        outDecoded.rowPitch = record.rowPitch;
        outDecoded.depthPitch = record.depthPitch;
        outDecoded.finalState = static_cast<ResourceStates::Mask>(record.finalState);
        outDecoded.uploadAspect = static_cast<TextureUploadAspect::Enum>(record.aspect);
        if(decodedRecord && (record.blobSizeBytes == 0u || record.blobOffsetBytes != inOutNextBlobOffset || record.blobSizeBytes > blobBytes.size() - inOutNextBlobOffset))
            return fail(GpuCommandIrStreamValidationError::InvalidBlobRange);
        if(decodedRecord)
            nextBlobOffset += static_cast<usize>(record.blobSizeBytes);
        break;
    }
    default:
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Known command IR opcode lost its decoder"));
        return fail(GpuCommandIrStreamValidationError::UnsupportedOpcode);
    }

    if(!decodedRecord || !ValidateBuiltinRecord(outDecoded))
        return fail(GpuCommandIrStreamValidationError::InvalidRecord);

    inOutNextBlobOffset = nextBlobOffset;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<GpuCommandIrBuiltinTaskRecord, GpuCommandIrStreamReadStatus::Enum> GpuCommandIrStreamReader::nextBuiltinTask()noexcept{
    if(m_validation.failed())
        return MakeUnexpected(GpuCommandIrStreamReadStatus::Error);

    if(m_nextRecordIndex == m_recordCount){
        if(m_cursor != m_payloadEnd){
            fail(GpuCommandIrStreamValidationError::TrailingPayload, m_cursor, m_nextRecordIndex);
            return MakeUnexpected(GpuCommandIrStreamReadStatus::Error);
        }
        if(m_nextBlobOffset != m_blobBytes.size()){
            fail(GpuCommandIrStreamValidationError::InvalidBlobRange, m_payloadEnd, m_nextRecordIndex);
            return MakeUnexpected(GpuCommandIrStreamReadStatus::Error);
        }
        m_validation.byteOffset = m_payloadEnd;
        m_validation.recordIndex = m_nextRecordIndex;
        m_validation.complete = true;
        return MakeUnexpected(GpuCommandIrStreamReadStatus::End);
    }

    const usize recordOffset = m_cursor;
    if(m_payloadEnd - recordOffset < sizeof(GpuCommandIrHeader)){
        fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
        return MakeUnexpected(GpuCommandIrStreamReadStatus::Error);
    }
    GpuCommandIrHeader header;
    FOUNDATION_MEMCPY(&header, sizeof(header), m_commandBytes.data() + recordOffset, sizeof(header));
    if(header.byteSize < sizeof(GpuCommandIrHeader)){
        fail(GpuCommandIrStreamValidationError::InvalidRecordSize, recordOffset, m_nextRecordIndex);
        return MakeUnexpected(GpuCommandIrStreamReadStatus::Error);
    }

    const usize expectedByteSize = GpuCommandIrDetail::BuiltinRecordWireByteSize(header.opcode);
    if(expectedByteSize == 0u){
        fail(GpuCommandIrStreamValidationError::UnsupportedOpcode, recordOffset, m_nextRecordIndex);
        return MakeUnexpected(GpuCommandIrStreamReadStatus::Error);
    }
    if(header.byteSize != expectedByteSize){
        fail(GpuCommandIrStreamValidationError::InvalidRecordSize, recordOffset, m_nextRecordIndex);
        return MakeUnexpected(GpuCommandIrStreamReadStatus::Error);
    }
    if(expectedByteSize > m_payloadEnd - recordOffset){
        fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
        return MakeUnexpected(GpuCommandIrStreamReadStatus::Error);
    }

    GpuCommandIrBuiltinTaskRecord decoded;
    if(!GpuCommandIrDetail::DecodeAndValidateBuiltinWireRecord(
        header.opcode,
        m_commandBytes.data() + recordOffset,
        m_blobBytes,
        m_graphGeneration,
        m_planGeneration,
        recordOffset,
        m_nextRecordIndex,
        m_nextBlobOffset,
        m_validation,
        decoded
    ))
        return MakeUnexpected(GpuCommandIrStreamReadStatus::Error);

    m_cursor = recordOffset + expectedByteSize;
    ++m_nextRecordIndex;
    return decoded;
}

BinaryByteView GpuCommandIrStreamReader::blobBytes()const noexcept{
    if(m_validation.failed() || m_blobBytes.empty())
        return {};
    return m_blobBytes;
}

void GpuCommandIrStreamReader::fail(const GpuCommandIrStreamValidationError::Enum error, const usize byteOffset, const u64 recordIndex)noexcept{
    if(m_validation.failed())
        return;

    m_validation.error = error;
    m_validation.byteOffset = byteOffset;
    m_validation.recordIndex = recordIndex;
    m_validation.complete = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrStreamValidationResult ValidateGpuCommandIrStream(const BinaryByteView bytes)noexcept{
    GpuCommandIrDetail::WireStreamCursor cursor(bytes, {}, false);
    cursor.drain();
    return cursor.validation();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

