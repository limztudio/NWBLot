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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrStreamReader::GpuCommandIrStreamReader(const BinaryByteView bytes)noexcept{
    GpuCommandIrDetail::InitializeStreamCursor(
        bytes,
        {},
        false,
        m_validation,
        m_commandBytes,
        m_blobBytes,
        m_cursor,
        m_payloadEnd,
        m_graphGeneration,
        m_planGeneration,
        m_recordCount
    );
}

GpuCommandIrStreamReader::GpuCommandIrStreamReader(const BinaryByteView commandBytes, const BinaryByteView blobBytes)noexcept{
    GpuCommandIrDetail::InitializeStreamCursor(
        commandBytes,
        blobBytes,
        true,
        m_validation,
        m_commandBytes,
        m_blobBytes,
        m_cursor,
        m_payloadEnd,
        m_graphGeneration,
        m_planGeneration,
        m_recordCount
    );
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
        FailValidation(outValidation, error, recordOffset, recordIndex);
        return false;
    };
    const auto decodeCommonContext = [&](const GpuCommandIrRecordContext& rawContext)noexcept -> b8{
        const auto context = DecodeRecordContext(rawContext, graphGeneration, planGeneration);
        if(!context)
            return false;
        outDecoded.task = context->task;
        outDecoded.packet = context->packet;
        outDecoded.queue = context->queue;
        return true;
    };

    outDecoded = {};
    b8 decodedRecord = false;
    usize nextBlobOffset = inOutNextBlobOffset;
    switch(opcode){
    case GpuCommandIrWireOpcode::CopyBuffer:{
        const auto record = ReadWirePod<GpuCommandIrCopyBufferRecord>(recordBytes);
        outDecoded.opcode = GpuCommandIrOpcode::CopyBuffer;
        const auto sourceResource = DecodeResource(record.sourceResourceIndex, graphGeneration);
        const auto destinationResource = DecodeResource(record.destinationResourceIndex, graphGeneration);
        decodedRecord = decodeCommonContext(record.context) && sourceResource.has_value() && destinationResource.has_value() && record.dataSizeBytes != 0u;
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
        const auto record = ReadWirePod<GpuCommandIrCopyTextureRecord>(recordBytes);
        outDecoded.opcode = GpuCommandIrOpcode::CopyTexture;
        const auto sourceResource = DecodeResource(record.sourceResourceIndex, graphGeneration);
        const auto destinationResource = DecodeResource(record.destinationResourceIndex, graphGeneration);
        decodedRecord = decodeCommonContext(record.context) && sourceResource.has_value() && destinationResource.has_value();
        if(sourceResource)
            outDecoded.source = *sourceResource;
        if(destinationResource)
            outDecoded.destination = *destinationResource;
        outDecoded.sourceSlice = DecodeTextureSlice(record.sourceSlice);
        outDecoded.destinationSlice = DecodeTextureSlice(record.destinationSlice);
        break;
    }
    case GpuCommandIrWireOpcode::ClearBuffer:{
        const auto record = ReadWirePod<GpuCommandIrClearBufferRecord>(recordBytes);
        outDecoded.opcode = GpuCommandIrOpcode::ClearBuffer;
        const auto destinationResource = DecodeResource(record.destinationResourceIndex, graphGeneration);
        decodedRecord = decodeCommonContext(record.context) && destinationResource.has_value();
        if(destinationResource)
            outDecoded.destination = *destinationResource;
        outDecoded.uintClearValue = UIntColor(record.clearValue);
        break;
    }
    case GpuCommandIrWireOpcode::ClearTexture:{
        const auto record = ReadWirePod<GpuCommandIrClearTextureRecord>(recordBytes);
        outDecoded.opcode = GpuCommandIrOpcode::ClearTexture;
        const auto destinationResource = DecodeResource(record.destinationResourceIndex, graphGeneration);
        decodedRecord = decodeCommonContext(record.context) && destinationResource.has_value() && ValidateClearTextureWireFields(record);
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
        const auto record = ReadWirePod<GpuCommandIrClearTextureRectUIntRecord>(recordBytes);
        outDecoded.opcode = GpuCommandIrOpcode::ClearTextureRectUInt;
        const auto destinationResource = DecodeResource(record.destinationResourceIndex, graphGeneration);
        decodedRecord = decodeCommonContext(record.context) && destinationResource.has_value() && ValidateClearTextureRectUIntWireFields(record);
        if(destinationResource)
            outDecoded.destination = *destinationResource;
        outDecoded.destinationSubresources = DecodeSubresources(record.destinationSubresources);
        outDecoded.clearRect = DecodeRect(record.clearRect);
        outDecoded.uintClearValue = DecodeColor(record.uintClearValue);
        break;
    }
    case GpuCommandIrWireOpcode::UploadBuffer:{
        const auto record = ReadWirePod<GpuCommandIrUploadBufferRecord>(recordBytes);
        outDecoded.opcode = GpuCommandIrOpcode::UploadBuffer;
        const auto destinationResource = DecodeResource(record.destinationResourceIndex, graphGeneration);
        decodedRecord = decodeCommonContext(record.context) && destinationResource.has_value() && record.sourceUploadBlobIndex != Limit<u32>::s_Max && record.reserved == 0u;
        if(destinationResource)
            outDecoded.destination = *destinationResource;
        outDecoded.sourceUploadBlob = GpuUploadBlobId{ .generation = graphGeneration, .index = record.sourceUploadBlobIndex, };
        outDecoded.blobOffsetBytes = record.blobOffsetBytes;
        outDecoded.blobSizeBytes = record.blobSizeBytes;
        outDecoded.destinationOffsetBytes = record.destinationOffsetBytes;
        outDecoded.finalState = static_cast<ResourceStates::Mask>(record.finalState);
        if(decodedRecord && !ValidateUploadBlobRange(blobBytes, record.blobOffsetBytes, record.blobSizeBytes, nextBlobOffset))
            return fail(GpuCommandIrStreamValidationError::InvalidBlobRange);
        break;
    }
    case GpuCommandIrWireOpcode::UploadTexture:{
        const auto record = ReadWirePod<GpuCommandIrUploadTextureRecord>(recordBytes);
        outDecoded.opcode = GpuCommandIrOpcode::UploadTexture;
        const auto destinationResource = DecodeResource(record.destinationResourceIndex, graphGeneration);
        decodedRecord = decodeCommonContext(record.context) && destinationResource.has_value() && record.sourceUploadBlobIndex != Limit<u32>::s_Max && record.aspect < TextureUploadAspect::kCount && record.reserved0 == 0u && record.reserved1 == 0u;
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
        if(decodedRecord && !ValidateUploadBlobRange(blobBytes, record.blobOffsetBytes, record.blobSizeBytes, nextBlobOffset))
            return fail(GpuCommandIrStreamValidationError::InvalidBlobRange);
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

    if(m_nextRecordIndex == m_recordCount)
        return MakeUnexpected(
            GpuCommandIrDetail::CheckStreamEndOfStream(m_cursor, m_payloadEnd, m_nextBlobOffset, m_blobBytes.size(), m_nextRecordIndex, m_validation)
                ? GpuCommandIrStreamReadStatus::End
                : GpuCommandIrStreamReadStatus::Error
        );

    const usize recordOffset = m_cursor;
    GpuCommandIrHeader header;
    usize expectedByteSize = 0u;
    if(!GpuCommandIrDetail::ReadValidatedRecordHeader(m_commandBytes, recordOffset, m_payloadEnd, m_nextRecordIndex, false, m_validation, header, expectedByteSize))
        return MakeUnexpected(GpuCommandIrStreamReadStatus::Error);

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
    GpuCommandIrDetail::FailValidation(m_validation, error, byteOffset, recordIndex);
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

