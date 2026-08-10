// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuCommandIrCapture::reset()noexcept{
    m_records.clear();
    m_graphGeneration = 0u;
}

const GpuCommandIrBuiltinTaskRecord* GpuCommandIrCapture::recordAt(const usize index)const noexcept{
    return index < m_records.size() ? &m_records[index] : nullptr;
}

void GpuCommandIrCapture::rollback(const usize recordCount)noexcept{
    if(recordCount > m_records.size())
        return;
    m_records.resize(recordCount);
    if(m_records.empty())
        m_graphGeneration = 0u;
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
        if(
            !record.source.valid()
            || record.source.generation != record.task.generation
            || record.dataSizeBytes == 0u
        )
            return false;
        break;
    case GpuCommandIrOpcode::CopyTexture:
        if(!record.source.valid() || record.source.generation != record.task.generation)
            return false;
        break;
    case GpuCommandIrOpcode::ClearBuffer:
        break;
    case GpuCommandIrOpcode::ClearTexture:
        if(
            record.clearTextureValueType >= GpuClearTextureTaskValueType::kCount
            || record.destinationSubresources.numMipLevels == 0u
            || record.destinationSubresources.numArraySlices == 0u
            || (
                record.clearTextureValueType == GpuClearTextureTaskValueType::DepthStencil
                && !record.clearDepth
                && !record.clearStencil
            )
        )
            return false;
        break;
    default:
        return false;
    }

    if(m_graphGeneration == 0u)
        m_graphGeneration = record.task.generation;
    else if(m_graphGeneration != record.task.generation)
        return false;

    m_records.push_back(record);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
