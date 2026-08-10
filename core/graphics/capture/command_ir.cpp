// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidateBuiltinRecord(const GpuCommandIrBuiltinTaskRecord& record)noexcept{
    if(
        record.opcode >= GpuCommandIrOpcode::kCount
        || !record.task.valid()
        || !record.packet.valid()
        || !record.queue.valid()
        || record.task.generation != record.packet.generation
        || !record.destination.valid()
        || record.destination.generation != record.task.generation
    )
        return false;

    switch(record.opcode){
    case GpuCommandIrOpcode::CopyBuffer:
        return record.source.valid()
            && record.source.generation == record.task.generation
            && record.dataSizeBytes != 0u
        ;
    case GpuCommandIrOpcode::CopyTexture:
        return record.source.valid() && record.source.generation == record.task.generation;
    case GpuCommandIrOpcode::ClearBuffer:
        return true;
    case GpuCommandIrOpcode::ClearTexture:
        return record.clearTextureValueType < GpuClearTextureTaskValueType::kCount
            && record.destinationSubresources.numMipLevels != 0u
            && record.destinationSubresources.numArraySlices != 0u
            && (
                record.clearTextureValueType != GpuClearTextureTaskValueType::DepthStencil
                || record.clearDepth
                || record.clearStencil
            )
        ;
    default:
        return false;
    }
}

template<typename RecordT>
[[nodiscard]] static bool AppendEncodedRecord(GraphicsBytes& outBytes, const RecordT& record){
    static_assert(IsStandardLayout_V<RecordT>, "Command IR records must be standard layout");
    static_assert(IsTriviallyCopyable_V<RecordT>, "Command IR records must be trivially copyable");
    static_assert(sizeof(RecordT) <= Limit<u16>::s_Max, "Command IR record exceeds its u16 byte-size field");

    if(
        outBytes.size() < sizeof(GpuCommandIrStreamHeader)
        || !BinaryDetail::CanAppendBytes(outBytes, sizeof(RecordT))
        || outBytes.size() - sizeof(GpuCommandIrStreamHeader) > Limit<u64>::s_Max - sizeof(RecordT)
    )
        return false;

    BinaryDetail::ReserveAppendBytesIfSupported(outBytes, sizeof(RecordT));
    AppendPOD(outBytes, record);
    return true;
}

[[nodiscard]] static GpuCommandIrRecordContext EncodeContext(const GpuCommandIrBuiltinTaskRecord& record)noexcept{
    return GpuCommandIrRecordContext{
        .taskIndex = record.task.index,
        .packetIndex = record.packet.index,
        .queueIndex = record.queue.index,
        .queueDeviceGeneration = record.queue.deviceGeneration,
    };
}

[[nodiscard]] static GpuCommandIrTextureSlice EncodeTextureSlice(const TextureSlice& slice)noexcept{
    return GpuCommandIrTextureSlice{
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

[[nodiscard]] static GpuCommandIrTextureSubresourceSet EncodeSubresources(const TextureSubresourceSet& subresources)noexcept{
    return GpuCommandIrTextureSubresourceSet{
        .baseMipLevel = subresources.baseMipLevel,
        .numMipLevels = subresources.numMipLevels,
        .baseArraySlice = subresources.baseArraySlice,
        .numArraySlices = subresources.numArraySlices,
    };
}

[[nodiscard]] static GpuCommandIrFloatColor EncodeColor(const Color& color)noexcept{
    return GpuCommandIrFloatColor{
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
    };
}

[[nodiscard]] static GpuCommandIrUIntColor EncodeColor(const UIntColor& color)noexcept{
    return GpuCommandIrUIntColor{
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
    };
}

[[nodiscard]] static GpuCommandIrIntColor EncodeColor(const IntColor& color)noexcept{
    return GpuCommandIrIntColor{
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
    };
}

template<typename RecordT>
static void InitializeRecord(RecordT& record, const GpuCommandIrWireOpcode::Enum opcode)noexcept{
    static_assert(IsTriviallyCopyable_V<RecordT>, "Command IR records must be trivially copyable");
    NWB_MEMSET(&record, 0, sizeof(record));
    record.header.opcode = opcode;
    record.header.byteSize = static_cast<u16>(sizeof(record));
}

[[nodiscard]] static bool DecodeContext(
    const GpuCommandIrRecordContext& context,
    const u64 graphGeneration,
    GpuCommandIrBuiltinTaskRecord& outRecord
)noexcept{
    outRecord.task = GpuTaskId{ context.taskIndex, graphGeneration };
    outRecord.packet = GpuSubmissionPacketId{ context.packetIndex, graphGeneration };
    outRecord.queue = GpuPhysicalQueueId{ context.queueIndex, context.queueDeviceGeneration };
    return outRecord.task.valid() && outRecord.packet.valid() && outRecord.queue.valid();
}

[[nodiscard]] static bool DecodeResource(
    const u32 resourceIndex,
    const u64 graphGeneration,
    GpuGraphResourceId& outResource
)noexcept{
    outResource = GpuGraphResourceId{ resourceIndex, graphGeneration };
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrStreamReader::GpuCommandIrStreamReader(const BinaryByteView bytes)noexcept
    : m_bytes(bytes)
{
    if(m_bytes.size() != 0u && !m_bytes.data()){
        fail(GpuCommandIrStreamValidationError::NullData, 0u, Limit<u64>::s_Max);
        return;
    }
    if(m_bytes.size() < sizeof(GpuCommandIrStreamHeader)){
        fail(GpuCommandIrStreamValidationError::TruncatedStreamHeader, 0u, Limit<u64>::s_Max);
        return;
    }

    usize cursor = 0u;
    GpuCommandIrStreamHeader header;
    if(!ReadPOD(m_bytes, cursor, header)){
        fail(GpuCommandIrStreamValidationError::TruncatedStreamHeader, 0u, Limit<u64>::s_Max);
        return;
    }
    if(header.magic != s_GpuCommandIrStreamMagic){
        fail(GpuCommandIrStreamValidationError::InvalidMagic, 0u, Limit<u64>::s_Max);
        return;
    }
    if(header.version != s_GpuCommandIrStreamVersion){
        fail(GpuCommandIrStreamValidationError::UnsupportedVersion, 0u, Limit<u64>::s_Max);
        return;
    }
    if(header.reserved != 0u){
        fail(GpuCommandIrStreamValidationError::InvalidHeaderReserved, 0u, Limit<u64>::s_Max);
        return;
    }
    if(header.payloadBytes > static_cast<u64>(Limit<usize>::s_Max)){
        fail(GpuCommandIrStreamValidationError::PayloadSizeMismatch, 0u, Limit<u64>::s_Max);
        return;
    }

    const usize payloadBytes = static_cast<usize>(header.payloadBytes);
    if(payloadBytes != m_bytes.size() - sizeof(GpuCommandIrStreamHeader)){
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
    if(header.recordCount > header.payloadBytes / sizeof(GpuCommandIrHeader)){
        fail(GpuCommandIrStreamValidationError::InvalidRecordCount, 0u, Limit<u64>::s_Max);
        return;
    }

    m_cursor = cursor;
    m_payloadEnd = m_bytes.size();
    m_graphGeneration = header.graphGeneration;
    m_recordCount = header.recordCount;
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
    using namespace __hidden_gpu_command_ir;
    switch(header.opcode){
    case GpuCommandIrWireOpcode::CopyBuffer:{
        GpuCommandIrCopyBufferRecord record;
        recordCursor = recordOffset;
        if(!ReadPOD(m_bytes, recordCursor, record)){
            fail(GpuCommandIrStreamValidationError::TruncatedRecord, recordOffset, m_nextRecordIndex);
            return GpuCommandIrStreamReadStatus::Error;
        }
        decoded.opcode = GpuCommandIrOpcode::CopyBuffer;
        decodedRecord = DecodeContext(record.context, m_graphGeneration, decoded)
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
        decodedRecord = DecodeContext(record.context, m_graphGeneration, decoded)
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
        decodedRecord = DecodeContext(record.context, m_graphGeneration, decoded)
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
        decodedRecord = DecodeContext(record.context, m_graphGeneration, decoded)
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
    default:
        NWB_ASSERT_MSG(false, NWB_TEXT("Known command IR opcode lost its decoder"));
        fail(GpuCommandIrStreamValidationError::UnsupportedOpcode, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }

    if(!decodedRecord || !ValidateBuiltinRecord(decoded)){
        fail(GpuCommandIrStreamValidationError::InvalidRecord, recordOffset, m_nextRecordIndex);
        return GpuCommandIrStreamReadStatus::Error;
    }

    m_cursor = recordCursor;
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


void GpuCommandIrCapture::reset()noexcept{
    m_records.clear();
    m_commandBytes.resize(sizeof(GpuCommandIrStreamHeader));
    m_graphGeneration = 0u;
    writeStreamHeader();
}

const GpuCommandIrBuiltinTaskRecord* GpuCommandIrCapture::recordAt(const usize index)const noexcept{
    return index < m_records.size() ? &m_records[index] : nullptr;
}

void GpuCommandIrCapture::rollback(const usize recordCount)noexcept{
    if(recordCount > m_records.size())
        return;

    const usize byteOffset = byteOffsetAfterRecordCount(recordCount);
    if(byteOffset == Limit<usize>::s_Max){
        NWB_ASSERT_MSG(false, NWB_TEXT("Command IR capture stream lost a record boundary"));
        return;
    }

    m_records.resize(recordCount);
    m_commandBytes.resize(byteOffset);
    m_graphGeneration = m_records.empty() ? 0u : m_records[0u].task.generation;
    writeStreamHeader();
}

bool GpuCommandIrCapture::captureCopyBuffer(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue,
    const GpuGraphResourceId source,
    const u64 sourceOffsetBytes,
    const GpuGraphResourceId destination,
    const u64 destinationOffsetBytes,
    const u64 dataSizeBytes
){
    GpuCommandIrBuiltinTaskRecord record;
    record.opcode = GpuCommandIrOpcode::CopyBuffer;
    record.task = task;
    record.packet = packet;
    record.queue = queue;
    record.source = source;
    record.destination = destination;
    record.sourceOffsetBytes = sourceOffsetBytes;
    record.destinationOffsetBytes = destinationOffsetBytes;
    record.dataSizeBytes = dataSizeBytes;
    return append(record);
}

bool GpuCommandIrCapture::captureCopyTexture(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue,
    const GpuGraphResourceId source,
    const TextureSlice sourceSlice,
    const GpuGraphResourceId destination,
    const TextureSlice destinationSlice
){
    GpuCommandIrBuiltinTaskRecord record;
    record.opcode = GpuCommandIrOpcode::CopyTexture;
    record.task = task;
    record.packet = packet;
    record.queue = queue;
    record.source = source;
    record.destination = destination;
    record.sourceSlice = sourceSlice;
    record.destinationSlice = destinationSlice;
    return append(record);
}

bool GpuCommandIrCapture::captureClearBuffer(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue,
    const GpuGraphResourceId destination,
    const u32 clearValue
){
    GpuCommandIrBuiltinTaskRecord record;
    record.opcode = GpuCommandIrOpcode::ClearBuffer;
    record.task = task;
    record.packet = packet;
    record.queue = queue;
    record.destination = destination;
    record.uintClearValue = UIntColor(clearValue);
    return append(record);
}

bool GpuCommandIrCapture::captureClearTexture(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue,
    const GpuGraphResourceId destination,
    const GpuClearTextureTaskDesc& clearDesc
){
    GpuCommandIrBuiltinTaskRecord record;
    record.opcode = GpuCommandIrOpcode::ClearTexture;
    record.task = task;
    record.packet = packet;
    record.queue = queue;
    record.destination = destination;
    record.destinationSubresources = clearDesc.subresources;
    record.clearTextureValueType = clearDesc.valueType;
    record.floatClearValue = clearDesc.floatValue;
    record.uintClearValue = clearDesc.uintValue;
    record.intClearValue = clearDesc.intValue;
    record.depthClearValue = clearDesc.depthValue;
    record.stencilClearValue = clearDesc.stencilValue;
    record.clearDepth = clearDesc.clearDepth;
    record.clearStencil = clearDesc.clearStencil;
    return append(record);
}

bool GpuCommandIrCapture::append(const GpuCommandIrBuiltinTaskRecord& record){
    if(!__hidden_gpu_command_ir::ValidateBuiltinRecord(record))
        return false;

    if(m_graphGeneration != 0u && m_graphGeneration != record.task.generation)
        return false;
    const usize recordCountBefore = m_records.size();
    const usize byteCountBefore = m_commandBytes.size();
    try{
        // Keep the legacy inspection cache and the POD stream transactional as one capture record. In particular,
        // an allocator failure in either update must not strand an uncounted byte record in the stream.
        m_records.push_back(record);
        if(!appendCommandBytes(record)){
            m_records.pop_back();
            return false;
        }
    }
    catch(...){
        m_records.resize(recordCountBefore);
        m_commandBytes.resize(byteCountBefore);
        return false;
    }

    if(m_graphGeneration == 0u)
        m_graphGeneration = record.task.generation;
    writeStreamHeader();
    return true;
}

bool GpuCommandIrCapture::appendCommandBytes(const GpuCommandIrBuiltinTaskRecord& record){
    using namespace __hidden_gpu_command_ir;

    switch(record.opcode){
    case GpuCommandIrOpcode::CopyBuffer:{
        GpuCommandIrCopyBufferRecord encoded;
        InitializeRecord(encoded, GpuCommandIrWireOpcode::CopyBuffer);
        encoded.context = EncodeContext(record);
        encoded.sourceResourceIndex = record.source.index;
        encoded.destinationResourceIndex = record.destination.index;
        encoded.sourceOffsetBytes = record.sourceOffsetBytes;
        encoded.destinationOffsetBytes = record.destinationOffsetBytes;
        encoded.dataSizeBytes = record.dataSizeBytes;
        return AppendEncodedRecord(m_commandBytes, encoded);
    }
    case GpuCommandIrOpcode::CopyTexture:{
        GpuCommandIrCopyTextureRecord encoded;
        InitializeRecord(encoded, GpuCommandIrWireOpcode::CopyTexture);
        encoded.context = EncodeContext(record);
        encoded.sourceResourceIndex = record.source.index;
        encoded.destinationResourceIndex = record.destination.index;
        encoded.sourceSlice = EncodeTextureSlice(record.sourceSlice);
        encoded.destinationSlice = EncodeTextureSlice(record.destinationSlice);
        return AppendEncodedRecord(m_commandBytes, encoded);
    }
    case GpuCommandIrOpcode::ClearBuffer:{
        GpuCommandIrClearBufferRecord encoded;
        InitializeRecord(encoded, GpuCommandIrWireOpcode::ClearBuffer);
        encoded.context = EncodeContext(record);
        encoded.destinationResourceIndex = record.destination.index;
        encoded.clearValue = record.uintClearValue.r;
        return AppendEncodedRecord(m_commandBytes, encoded);
    }
    case GpuCommandIrOpcode::ClearTexture:{
        GpuCommandIrClearTextureRecord encoded;
        InitializeRecord(encoded, GpuCommandIrWireOpcode::ClearTexture);
        encoded.context = EncodeContext(record);
        encoded.destinationResourceIndex = record.destination.index;
        encoded.destinationSubresources = EncodeSubresources(record.destinationSubresources);
        encoded.floatClearValue = EncodeColor(record.floatClearValue);
        encoded.uintClearValue = EncodeColor(record.uintClearValue);
        encoded.intClearValue = EncodeColor(record.intClearValue);
        encoded.depthClearValue = record.depthClearValue;
        encoded.stencilClearValue = record.stencilClearValue;
        encoded.clearTextureValueType = static_cast<u8>(record.clearTextureValueType);
        // Color clears retain legacy inspection fields, but native color-clear lowering ignores aspect flags. The
        // stream is canonical and only encodes depth/stencil selection for a depth/stencil clear command.
        if(record.clearTextureValueType == GpuClearTextureTaskValueType::DepthStencil){
            if(record.clearDepth)
                encoded.clearFlags = static_cast<GpuCommandIrClearTextureFlag::Mask>(
                    static_cast<u8>(encoded.clearFlags) | GpuCommandIrClearTextureFlag::ClearDepth
                );
            if(record.clearStencil)
                encoded.clearFlags = static_cast<GpuCommandIrClearTextureFlag::Mask>(
                    static_cast<u8>(encoded.clearFlags) | GpuCommandIrClearTextureFlag::ClearStencil
                );
        }
        return AppendEncodedRecord(m_commandBytes, encoded);
    }
    default:
        return false;
    }
}

usize GpuCommandIrCapture::byteOffsetAfterRecordCount(const usize recordCount)const noexcept{
    if(recordCount > m_records.size() || m_commandBytes.size() < sizeof(GpuCommandIrStreamHeader))
        return Limit<usize>::s_Max;

    const BinaryByteView bytes = commandBytes();
    usize cursor = sizeof(GpuCommandIrStreamHeader);
    for(usize recordIndex = 0u; recordIndex < recordCount; ++recordIndex){
        const usize recordOffset = cursor;
        GpuCommandIrHeader header;
        if(
            !ReadPOD(bytes, cursor, header)
            || header.byteSize < sizeof(header)
            || header.byteSize > bytes.size() - recordOffset
        )
            return Limit<usize>::s_Max;
        cursor = recordOffset + header.byteSize;
    }
    return cursor;
}

void GpuCommandIrCapture::writeStreamHeader()noexcept{
    NWB_ASSERT(m_commandBytes.size() >= sizeof(GpuCommandIrStreamHeader));
    GpuCommandIrStreamHeader header;
    header.graphGeneration = m_graphGeneration;
    header.recordCount = static_cast<u64>(m_records.size());
    header.payloadBytes = static_cast<u64>(m_commandBytes.size() - sizeof(GpuCommandIrStreamHeader));
    NWB_MEMCPY(m_commandBytes.data(), sizeof(header), &header, sizeof(header));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
