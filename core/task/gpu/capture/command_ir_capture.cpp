// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ValidateBuiltinRecord(const GpuCommandIrBuiltinTaskRecord& record)noexcept{
    constexpr u32 allowedResourceStates = (static_cast<u32>(ResourceStates::ConvertCoopVecMatrixOutput) << 1u) - 1u;
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
    case GpuCommandIrOpcode::UploadBuffer:
        return record.sourceUploadBlob.valid()
            && record.sourceUploadBlob.generation == record.task.generation
            && record.blobSizeBytes != 0u
            && record.finalState != ResourceStates::Unknown
            && (static_cast<u32>(record.finalState) & ~allowedResourceStates) == 0u
        ;
    case GpuCommandIrOpcode::UploadTexture:
        return record.sourceUploadBlob.valid()
            && record.sourceUploadBlob.generation == record.task.generation
            && record.blobSizeBytes != 0u
            && record.finalState != ResourceStates::Unknown
            && (static_cast<u32>(record.finalState) & ~allowedResourceStates) == 0u
            && record.uploadAspect < TextureUploadAspect::kCount
            && record.destinationSlice.x == 0u
            && record.destinationSlice.y == 0u
            && record.destinationSlice.z == 0u
            && record.destinationSlice.width != 0u
            && record.destinationSlice.height != 0u
            && record.destinationSlice.depth != 0u
            && record.destinationSlice.width != TextureSlice::s_AllDimensions
            && record.destinationSlice.height != TextureSlice::s_AllDimensions
            && record.destinationSlice.depth != TextureSlice::s_AllDimensions
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


[[nodiscard]] static u64 AllocateCaptureIdentity()noexcept{
    static Atomic<u64> s_NextIdentity{ 1u };
    u64 identity = s_NextIdentity.load(MemoryOrder::relaxed);
    while(identity != 0u && identity != Limit<u64>::s_Max){
        if(s_NextIdentity.compare_exchange_weak(identity, identity + 1u, MemoryOrder::relaxed, MemoryOrder::relaxed))
            return identity;
    }
    GLOBAL_FATAL_ASSERT_MSG(false, "Command IR capture identity space is exhausted");
    TerminateInvariant();
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

    ContainerDetail::ReserveGrowingCapacity(outBytes, outBytes.size() + sizeof(RecordT));
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
    GLOBAL_MEMSET(&record, 0, sizeof(record));
    record.header.opcode = opcode;
    record.header.byteSize = static_cast<u16>(sizeof(record));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrOwnedStream::GpuCommandIrOwnedStream(GraphicsArena& arena)
    : m_arena(arena)
    , m_bytes(arena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrCapture::GpuCommandIrCapture(GraphicsArena& arena)
    : m_recordEndOffsets(arena)
    , m_blobEndOffsets(arena)
    , m_recordSerials(arena)
    , m_commandBytes(arena)
    , m_blobBytes(arena)
    , m_packedBytes(arena)
    , m_arena(arena)
    , m_ownerIdentity(__hidden_gpu_command_ir_capture::AllocateCaptureIdentity())
{
    m_commandBytes.resize(sizeof(GpuCommandIrStreamHeader));
    writeStreamHeader();
}

BinaryByteView GpuCommandIrCapture::commandBytes()const{
    if(m_blobBytes.empty())
        return BinaryByteView{ m_commandBytes.data(), m_commandBytes.size() };

    if(m_packedDirty){
        GLOBAL_FATAL_ASSERT_MSG(
            m_commandBytes.size() <= Limit<usize>::s_Max - m_blobBytes.size(),
            "Command IR stream size exceeded addressable storage"
        );
        const usize totalSize = m_commandBytes.size() + m_blobBytes.size();
        m_packedBytes.resize(totalSize);
        GLOBAL_MEMCPY(m_packedBytes.data(), totalSize, m_commandBytes.data(), m_commandBytes.size());
        GLOBAL_MEMCPY(
            m_packedBytes.data() + m_commandBytes.size(),
            m_blobBytes.size(),
            m_blobBytes.data(),
            m_blobBytes.size()
        );
        m_packedDirty = false;
    }
    return BinaryByteView{ m_packedBytes.data(), m_packedBytes.size() };
}

bool GpuCommandIrCapture::exportOwned(GpuCommandIrOwnedStream& outStream)const{
    if(m_commandBytes.size() > Limit<usize>::s_Max - m_blobBytes.size())
        return false;
    const usize totalSize = m_commandBytes.size() + m_blobBytes.size();
    GraphicsBytes candidate(outStream.m_arena);
    candidate.resize(totalSize);
    GLOBAL_MEMCPY(candidate.data(), totalSize, m_commandBytes.data(), m_commandBytes.size());
    if(!m_blobBytes.empty()){
        GLOBAL_MEMCPY(
            candidate.data() + m_commandBytes.size(),
            m_blobBytes.size(),
            m_blobBytes.data(),
            m_blobBytes.size()
        );
    }
    GpuCommandIrRasterOwnerTable* ownerCandidate = nullptr;
    if(m_rasterOwners && (!m_rasterOwners->stateOwners.empty() || !m_rasterOwners->heapOwners.empty())){
        GpuCommandIrRasterOwnerTable staged(outStream.m_arena);
        staged.stateOwners = m_rasterOwners->stateOwners;
        staged.heapOwners = m_rasterOwners->heapOwners;
        ownerCandidate = NewArenaObject<GpuCommandIrRasterOwnerTable>(outStream.m_arena, outStream.m_arena);
        ownerCandidate->stateOwners.swap(staged.stateOwners);
        ownerCandidate->heapOwners.swap(staged.heapOwners);
    }
    outStream.m_bytes.swap(candidate);
    GpuCommandIrRasterOwnerTable* const previousOwners = outStream.m_rasterOwners;
    outStream.m_rasterOwners = ownerCandidate;
    if(previousOwners)
        DestroyArenaObjectNoexcept(outStream.m_arena, previousOwners);
    return true;
}

GpuCommandIrCaptureCheckpoint GpuCommandIrCapture::checkpoint()const noexcept{
    GpuCommandIrCaptureCheckpoint result;
    result.m_ownerIdentity = m_ownerIdentity;
    result.m_resetEpoch = m_resetEpoch;
    result.m_recordCount = m_recordEndOffsets.size();
    result.m_prefixSerial = m_recordSerials.empty() ? 0u : m_recordSerials.back();
    result.m_graphGeneration = m_graphGeneration;
    result.m_planGeneration = m_planGeneration;
    result.m_recordingAttemptGeneration = m_recordingAttemptGeneration;
    result.m_commandSize = m_commandBytes.size();
    result.m_blobSize = m_blobBytes.size();
    result.m_rasterStateOwnerCount = m_rasterOwners ? m_rasterOwners->stateOwners.size() : 0u;
    result.m_rasterHeapOwnerCount = m_rasterOwners ? m_rasterOwners->heapOwners.size() : 0u;
    return result;
}

bool GpuCommandIrCapture::rollback(const GpuCommandIrCaptureCheckpoint& target)noexcept{
    usize prefixStateOwners = 0u;
    usize prefixHeapOwners = 0u;
    if(m_rasterOwners){
        while(
            prefixStateOwners < m_rasterOwners->stateOwners.size()
            && m_rasterOwners->stateOwners[prefixStateOwners].recordIndex < target.m_recordCount
        )
            ++prefixStateOwners;
        while(
            prefixHeapOwners < m_rasterOwners->heapOwners.size()
            && m_rasterOwners->heapOwners[prefixHeapOwners].recordIndex < target.m_recordCount
        )
            ++prefixHeapOwners;
    }
    if(
        target.m_ownerIdentity != m_ownerIdentity
        || target.m_resetEpoch != m_resetEpoch
        || target.m_recordingAttemptGeneration != m_recordingAttemptGeneration
        || target.m_recordCount > m_recordEndOffsets.size()
        || target.m_commandSize != (
            target.m_recordCount == 0u ? sizeof(GpuCommandIrStreamHeader) : m_recordEndOffsets[target.m_recordCount - 1u]
        )
        || target.m_blobSize != (target.m_recordCount == 0u ? 0u : m_blobEndOffsets[target.m_recordCount - 1u])
        || target.m_rasterStateOwnerCount != prefixStateOwners
        || target.m_rasterHeapOwnerCount != prefixHeapOwners
        || target.m_prefixSerial != (target.m_recordCount == 0u ? 0u : m_recordSerials[target.m_recordCount - 1u])
        || (target.m_recordCount != 0u && (
            target.m_graphGeneration != m_graphGeneration || target.m_planGeneration != m_planGeneration
        ))
    )
        return false;

    rollbackPrefix(
        target.m_recordCount,
        target.m_recordCount == 0u ? 0u : target.m_graphGeneration,
        target.m_recordCount == 0u ? 0u : target.m_planGeneration,
        target.m_recordCount == 0u ? 0u : target.m_recordingAttemptGeneration
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuCommandIrCapture::reset()noexcept{
    static_assert(noexcept(m_recordEndOffsets.clear()));
    static_assert(IsNothrowDestructible_V<GraphicsBytes::value_type>);
    GLOBAL_FATAL_ASSERT_MSG(
        m_commandBytes.size() >= sizeof(GpuCommandIrStreamHeader),
        "Command IR capture reset requires its reserved stream header storage"
    );
    if(m_commandBytes.size() < sizeof(GpuCommandIrStreamHeader))
        TerminateInvariant();

    // Shrinking resize keeps capacity without allocating.
    m_recordEndOffsets.clear();
    m_blobEndOffsets.clear();
    m_recordSerials.clear();
    m_commandBytes.resize(sizeof(GpuCommandIrStreamHeader));
    m_blobBytes.clear();
    if(m_rasterOwners){
        m_rasterOwners->stateOwners.clear();
        m_rasterOwners->heapOwners.clear();
    }
    m_packedDirty = true;
    if(m_resetEpoch == Limit<u64>::s_Max){
        GLOBAL_FATAL_ASSERT_MSG(false, "Command IR capture reset epoch exhausted");
        TerminateInvariant();
    }
    ++m_resetEpoch;
    m_graphGeneration = 0u;
    m_planGeneration = 0u;
    m_recordingAttemptGeneration = 0u;
    writeStreamHeader();
}

bool GpuCommandIrCapture::beginRecordingAttempt(const u64 recordingAttemptGeneration)noexcept{
    if(recordingAttemptGeneration == 0u)
        return false;
    if(!m_recordEndOffsets.empty() && m_recordingAttemptGeneration != recordingAttemptGeneration)
        return false;
    m_recordingAttemptGeneration = recordingAttemptGeneration;
    return true;
}

void GpuCommandIrCapture::rollback(const usize recordCount)noexcept{
    if(recordCount > m_recordEndOffsets.size())
        return;

    rollbackPrefix(
        recordCount,
        recordCount == 0u ? 0u : m_graphGeneration,
        recordCount == 0u ? 0u : m_planGeneration,
        recordCount == 0u ? 0u : m_recordingAttemptGeneration
    );
}

void GpuCommandIrCapture::rollbackPrefix(
    const usize recordCount,
    const u64 graphGeneration,
    const u64 planGeneration,
    const u64 attemptGeneration
)noexcept{
    const usize byteOffset = recordCount == 0u ? sizeof(GpuCommandIrStreamHeader) : m_recordEndOffsets[recordCount - 1u];
    const usize blobEnd = recordCount == 0u ? 0u : m_blobEndOffsets[recordCount - 1u];
    static_assert(IsNothrowDestructible_V<usize>);
    static_assert(IsNothrowDestructible_V<GraphicsBytes::value_type>);
    // Both resizes only shrink, so they cannot allocate.
    m_recordEndOffsets.resize(recordCount);
    m_blobEndOffsets.resize(recordCount);
    m_recordSerials.resize(recordCount);
    m_commandBytes.resize(byteOffset);
    m_blobBytes.resize(blobEnd);
    if(m_rasterOwners){
        while(!m_rasterOwners->stateOwners.empty() && m_rasterOwners->stateOwners.back().recordIndex >= recordCount)
            m_rasterOwners->stateOwners.pop_back();
        while(!m_rasterOwners->heapOwners.empty() && m_rasterOwners->heapOwners.back().recordIndex >= recordCount)
            m_rasterOwners->heapOwners.pop_back();
    }
    m_graphGeneration = graphGeneration;
    m_planGeneration = planGeneration;
    m_recordingAttemptGeneration = attemptGeneration;
    if(recordCount == 0u){
        if(m_resetEpoch == Limit<u64>::s_Max){
            GLOBAL_FATAL_ASSERT_MSG(false, "Command IR capture rollback epoch exhausted");
            TerminateInvariant();
        }
        ++m_resetEpoch;
    }
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

    const usize nextRecordCount = m_recordEndOffsets.size() + 1u;
    if(
        nextRecordCount == 0u
        || m_nextSerial == Limit<u64>::s_Max
        || !BinaryDetail::CanStoreValueCount(m_recordEndOffsets, nextRecordCount)
        || !BinaryDetail::CanStoreValueCount(m_blobEndOffsets, nextRecordCount)
        || !BinaryDetail::CanStoreValueCount(m_recordSerials, nextRecordCount)
    )
        return false;

    // Reserve first so failure leaves both sequences unchanged.
    ContainerDetail::ReserveGrowingCapacity(m_recordEndOffsets, nextRecordCount);
    ContainerDetail::ReserveGrowingCapacity(m_blobEndOffsets, nextRecordCount);
    ContainerDetail::ReserveGrowingCapacity(m_recordSerials, nextRecordCount);

    if(!appendCommandBytes(record))
        return false;
    m_recordEndOffsets.push_back(m_commandBytes.size());
    m_blobEndOffsets.push_back(m_blobBytes.size());
    m_recordSerials.push_back(m_nextSerial++);

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
        // Color clears ignore aspect flags; depth/stencil selection is encoded only for depth/stencil clears.
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
    case GpuCommandIrOpcode::UploadBuffer:{
        GpuCommandIrUploadBufferRecord encoded;
        InitializeRecord(encoded, GpuCommandIrWireOpcode::UploadBuffer);
        encoded.context = EncodeContext(record);
        encoded.sourceUploadBlobIndex = record.sourceUploadBlob.index;
        encoded.destinationResourceIndex = record.destination.index;
        encoded.blobOffsetBytes = record.blobOffsetBytes;
        encoded.blobSizeBytes = record.blobSizeBytes;
        encoded.destinationOffsetBytes = record.destinationOffsetBytes;
        encoded.finalState = static_cast<u32>(record.finalState);
        return AppendEncodedRecord(m_commandBytes, encoded);
    }
    case GpuCommandIrOpcode::UploadTexture:{
        GpuCommandIrUploadTextureRecord encoded;
        InitializeRecord(encoded, GpuCommandIrWireOpcode::UploadTexture);
        encoded.context = EncodeContext(record);
        encoded.sourceUploadBlobIndex = record.sourceUploadBlob.index;
        encoded.destinationResourceIndex = record.destination.index;
        encoded.blobOffsetBytes = record.blobOffsetBytes;
        encoded.blobSizeBytes = record.blobSizeBytes;
        encoded.destinationSlice = EncodeTextureSlice(record.destinationSlice);
        encoded.rowPitch = record.rowPitch;
        encoded.depthPitch = record.depthPitch;
        encoded.finalState = static_cast<u32>(record.finalState);
        encoded.aspect = static_cast<u8>(record.uploadAspect);
        return AppendEncodedRecord(m_commandBytes, encoded);
    }
    default:
        return false;
    }
}

void GpuCommandIrCapture::writeStreamHeader()noexcept{
    GLOBAL_ASSERT(m_commandBytes.size() >= sizeof(GpuCommandIrStreamHeader));
    GpuCommandIrStreamHeader header;
    header.graphGeneration = m_graphGeneration;
    header.planGeneration = m_planGeneration;
    header.recordCount = static_cast<u64>(m_recordEndOffsets.size());
    header.commandBytes = static_cast<u64>(m_commandBytes.size() - sizeof(GpuCommandIrStreamHeader));
    header.blobBytes = static_cast<u64>(m_blobBytes.size());
    GLOBAL_MEMCPY(m_commandBytes.data(), sizeof(header), &header, sizeof(header));
    m_packedDirty = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

