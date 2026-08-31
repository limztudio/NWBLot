// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ValidateBuiltinRecord(const GpuCommandIrBuiltinTaskRecord& record)noexcept{
    if(
        record.opcode >= GpuCommandIrOpcode::kCount
        || !record.task.valid()
        || !record.packet.valid()
        || !record.queue.valid()
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
    case GpuCommandIrOpcode::ClearTextureRectUInt:
        return record.destinationSubresources.numMipLevels != 0u
            && record.destinationSubresources.numArraySlices != 0u
            && record.clearRect.maxX > record.clearRect.minX
            && record.clearRect.maxY > record.clearRect.minY
        ;
    default:
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir_capture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

[[nodiscard]] static GpuCommandIrRect EncodeRect(const Rect& rect)noexcept{
    return GpuCommandIrRect{
        .minX = rect.minX,
        .maxX = rect.maxX,
        .minY = rect.minY,
        .maxY = rect.maxY,
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuCommandIrCapture::reset()noexcept{
    m_records.clear();
    m_commandBytes.resize(sizeof(GpuCommandIrStreamHeader));
    m_graphGeneration = 0u;
    m_planGeneration = 0u;
    m_recordingAttemptGeneration = 0u;
    writeStreamHeader();
}

bool GpuCommandIrCapture::beginRecordingAttempt(const u64 recordingAttemptGeneration)noexcept{
    if(recordingAttemptGeneration == 0u)
        return false;
    if(!m_records.empty() && m_recordingAttemptGeneration != recordingAttemptGeneration)
        return false;
    m_recordingAttemptGeneration = recordingAttemptGeneration;
    return true;
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
    m_planGeneration = m_records.empty() ? 0u : m_records[0u].packet.generation;
    if(m_records.empty())
        m_recordingAttemptGeneration = 0u;
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

bool GpuCommandIrCapture::captureClearTextureRectUInt(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue,
    const GpuGraphResourceId destination,
    const GpuClearTextureRectUIntTaskDesc& clearDesc
){
    GpuCommandIrBuiltinTaskRecord record;
    record.opcode = GpuCommandIrOpcode::ClearTextureRectUInt;
    record.task = task;
    record.packet = packet;
    record.queue = queue;
    record.destination = destination;
    record.destinationSubresources = clearDesc.subresources;
    record.clearRect = clearDesc.rect;
    record.uintClearValue = clearDesc.uintValue;
    return append(record);
}

bool GpuCommandIrCapture::append(const GpuCommandIrBuiltinTaskRecord& record){
    if(!GpuCommandIrDetail::ValidateBuiltinRecord(record))
        return false;

    if(m_graphGeneration != 0u && m_graphGeneration != record.task.generation)
        return false;
    if(m_planGeneration != 0u && m_planGeneration != record.packet.generation)
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
    if(m_planGeneration == 0u)
        m_planGeneration = record.packet.generation;
    writeStreamHeader();
    return true;
}

bool GpuCommandIrCapture::appendCommandBytes(const GpuCommandIrBuiltinTaskRecord& record){
    using namespace __hidden_gpu_command_ir_capture;

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
    case GpuCommandIrOpcode::ClearTextureRectUInt:{
        GpuCommandIrClearTextureRectUIntRecord encoded;
        InitializeRecord(encoded, GpuCommandIrWireOpcode::ClearTextureRectUInt);
        encoded.context = EncodeContext(record);
        encoded.destinationResourceIndex = record.destination.index;
        encoded.destinationSubresources = EncodeSubresources(record.destinationSubresources);
        encoded.clearRect = EncodeRect(record.clearRect);
        encoded.uintClearValue = EncodeColor(record.uintClearValue);
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
    header.planGeneration = m_planGeneration;
    header.recordCount = static_cast<u64>(m_records.size());
    header.payloadBytes = static_cast<u64>(m_commandBytes.size() - sizeof(GpuCommandIrStreamHeader));
    NWB_MEMCPY(m_commandBytes.data(), sizeof(header), &header, sizeof(header));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

