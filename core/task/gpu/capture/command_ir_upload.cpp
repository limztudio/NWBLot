// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuCommandIrCapture::captureUploadBuffer(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue,
    const GpuUploadBlobId sourceBlob,
    const GpuGraphResourceId destination,
    const u64 destinationOffsetBytes,
    const BinaryByteView sourceBytes,
    const ResourceStates::Mask finalState
){
    GpuCommandIrBuiltinTaskRecord record;
    record.opcode = GpuCommandIrOpcode::UploadBuffer;
    record.task = task;
    record.packet = packet;
    record.queue = queue;
    record.sourceUploadBlob = sourceBlob;
    record.destination = destination;
    record.destinationOffsetBytes = destinationOffsetBytes;
    record.finalState = finalState;
    return appendUpload(record, sourceBytes);
}

bool GpuCommandIrCapture::captureUploadTexture(
    const GpuTaskId task,
    const GpuSubmissionPacketId packet,
    const GpuPhysicalQueueId queue,
    const GpuUploadBlobId sourceBlob,
    const GpuGraphResourceId destination,
    const TextureSlice destinationSlice,
    const usize rowPitch,
    const usize depthPitch,
    const TextureUploadAspect::Enum aspect,
    const BinaryByteView sourceBytes,
    const ResourceStates::Mask finalState
){
    GpuCommandIrBuiltinTaskRecord record;
    record.opcode = GpuCommandIrOpcode::UploadTexture;
    record.task = task;
    record.packet = packet;
    record.queue = queue;
    record.sourceUploadBlob = sourceBlob;
    record.destination = destination;
    record.destinationSlice = destinationSlice;
    record.rowPitch = static_cast<u64>(rowPitch);
    record.depthPitch = static_cast<u64>(depthPitch);
    record.uploadAspect = aspect;
    record.finalState = finalState;
    return appendUpload(record, sourceBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuCommandIrCapture::appendUpload(GpuCommandIrBuiltinTaskRecord record, const BinaryByteView bytes){
    if(bytes.empty() || !bytes.data() || m_blobBytes.size() > Limit<u64>::s_Max - bytes.size())
        return false;

    record.blobOffsetBytes = static_cast<u64>(m_blobBytes.size());
    record.blobSizeBytes = static_cast<u64>(bytes.size());
    if(!GpuCommandIrDetail::ValidateBuiltinRecord(record))
        return false;
    if(m_graphGeneration != 0u && m_graphGeneration != record.task.generation)
        return false;
    if(m_planGeneration != 0u && m_planGeneration != record.packet.generation)
        return false;

    const usize nextRecordCount = m_recordEndOffsets.size() + 1u;
    const usize recordSize = record.opcode == GpuCommandIrOpcode::UploadBuffer
        ? sizeof(GpuCommandIrUploadBufferRecord) : sizeof(GpuCommandIrUploadTextureRecord);
    if(
        nextRecordCount == 0u
        || m_nextSerial == Limit<u64>::s_Max
        || !BinaryDetail::CanStoreValueCount(m_recordEndOffsets, nextRecordCount)
        || !BinaryDetail::CanStoreValueCount(m_blobEndOffsets, nextRecordCount)
        || !BinaryDetail::CanStoreValueCount(m_recordSerials, nextRecordCount)
        || !BinaryDetail::CanAppendBytes(m_commandBytes, recordSize)
        || !BinaryDetail::CanAppendBytes(m_blobBytes, bytes.size())
        || m_commandBytes.size() + recordSize > m_commandBytes.max_size()
        || m_blobBytes.size() + bytes.size() > m_blobBytes.max_size()
        || m_commandBytes.size() - sizeof(GpuCommandIrStreamHeader) > Limit<u64>::s_Max - recordSize
    )
        return false;

    // All potentially throwing allocations happen before either byte sequence changes.
    ContainerDetail::ReserveGrowingCapacity(m_recordEndOffsets, nextRecordCount);
    ContainerDetail::ReserveGrowingCapacity(m_blobEndOffsets, nextRecordCount);
    ContainerDetail::ReserveGrowingCapacity(m_recordSerials, nextRecordCount);
    ContainerDetail::ReserveGrowingCapacity(m_commandBytes, m_commandBytes.size() + recordSize);
    ContainerDetail::ReserveGrowingCapacity(m_blobBytes, m_blobBytes.size() + bytes.size());

    if(!appendCommandBytes(record))
        return false;
    const usize oldBlobSize = m_blobBytes.size();
    m_blobBytes.resize(oldBlobSize + bytes.size());
    NWB_MEMCPY(m_blobBytes.data() + oldBlobSize, bytes.size(), bytes.data(), bytes.size());

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

