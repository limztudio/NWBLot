// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_internal.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u64> ComputeShaderTableByteSize(const u32 recordCount, const u32 handleSizeAligned, TStringView operation){
    if(recordCount == 0u || handleSizeAligned == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table record count or stride is invalid"), operation);
        return MakeUnexpected(Failure{});
    }
    if(static_cast<u64>(recordCount) > Limit<u64>::s_Max / static_cast<u64>(handleSizeAligned)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table size overflows"), operation);
        return MakeUnexpected(Failure{});
    }

    return static_cast<u64>(recordCount) * static_cast<u64>(handleSizeAligned);
}

Expected<u64> ComputeShaderTableAllocationByteSize(
    const u64 recordByteSize,
    const u32 baseAlignment
)noexcept{
    if(recordByteSize == 0u || baseAlignment == 0u || (baseAlignment & (baseAlignment - 1u)) != 0u)
        return MakeUnexpected(Failure{});

    const u64 alignmentPadding = static_cast<u64>(baseAlignment) - 1u;
    if(recordByteSize > Limit<u64>::s_Max - alignmentPadding)
        return MakeUnexpected(Failure{});

    return recordByteSize + alignmentPadding;
}

Expected<u64> ComputeShaderTableAlignedOffset(
    const u64 deviceAddress,
    const u64 allocationByteSize,
    const u64 recordByteSize,
    const u32 baseAlignment
)noexcept{
    if(
        deviceAddress == 0u
        || allocationByteSize == 0u
        || recordByteSize == 0u
        || baseAlignment == 0u
        || (baseAlignment & (baseAlignment - 1u)) != 0u
    )
        return MakeUnexpected(Failure{});

    const auto alignedAddress = AlignUpU64Checked(deviceAddress, static_cast<u64>(baseAlignment));
    if(!alignedAddress)
        return MakeUnexpected(Failure{});
    if(*alignedAddress < deviceAddress)
        return MakeUnexpected(Failure{});

    const u64 offset = *alignedAddress - deviceAddress;
    if(offset > allocationByteSize || recordByteSize > allocationByteSize - offset)
        return MakeUnexpected(Failure{});

    return offset;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingShaderTableHandle RayTracingPipeline::createShaderTable(){
    auto* sbt = NewArenaObject<ShaderTable>(m_context.objectArena, m_context, m_device);
    if(!sbt){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader table: object allocation failed"));
        return nullptr;
    }
    sbt->m_pipeline = Handle<RayTracingPipeline>(this, Handle<RayTracingPipeline>::deleter_type(&m_context.objectArena));
    return RayTracingShaderTableHandle(sbt, RayTracingShaderTableHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderTable::ShaderTable(const VulkanContext& context, Device& device)
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_missGroupIndices(context.objectArena)
    , m_hitGroupIndices(context.objectArena)
    , m_callableGroupIndices(context.objectArena)
    , m_context(context)
    , m_device(device)
{}
ShaderTable::~ShaderTable(){}

bool ShaderTable::setRayGenerationShader(const AStringView exportName){
    ScopedLock lock(m_mutex);

    const auto preflight = preflightShaderRecord(exportName, ShaderTableRecordKind::RayGeneration, 1u, NWB_TEXT("set ray generation shader"), NWB_TEXT("ray generation"));
    if(!preflight)
        return false;
    auto allocation = allocateSBTBuffer(*preflight, NWB_TEXT("set ray generation shader"), NWB_TEXT("ray generation"));
    if(!allocation)
        return false;
    BufferHandle& newBuffer = allocation->buffer;
    const u64 newOffset = allocation->offset;

    void* const mapped = m_device.mapBuffer(*newBuffer, CpuAccessMode::Write);
    if(!mapped){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to set ray generation shader: failed to map new SBT buffer"));
        return false;
    }

    auto* const recordBytes = static_cast<u8*>(mapped) + static_cast<usize>(newOffset);
    NWB_MEMSET(recordBytes, 0, static_cast<usize>(preflight->recordByteSize));
    const u8* const handle = m_pipeline->m_shaderGroupHandles.data() + preflight->handleOffset;
    NWB_MEMCPY(recordBytes, preflight->handleSizeAligned, handle, preflight->handleSize);
    m_device.unmapBuffer(*newBuffer);

    m_raygenBuffer = Move(newBuffer);
    m_raygenOffset = newOffset;
    return true;
}

u32 ShaderTable::addMissShader(const AStringView exportName){
    ScopedLock lock(m_mutex);
    return appendShaderRecord(
        exportName,
        ShaderTableRecordKind::Miss,
        m_missGroupIndices,
        m_missBuffer,
        m_missOffset,
        m_missCount,
        NWB_TEXT("add miss shader"),
        NWB_TEXT("miss"),
        NWB_TEXT("miss shader")
    );
}

u32 ShaderTable::addHitGroup(const AStringView exportName){
    ScopedLock lock(m_mutex);
    return appendShaderRecord(
        exportName,
        ShaderTableRecordKind::HitGroup,
        m_hitGroupIndices,
        m_hitBuffer,
        m_hitOffset,
        m_hitCount,
        NWB_TEXT("add hit group"),
        NWB_TEXT("hit"),
        NWB_TEXT("hit group")
    );
}

u32 ShaderTable::addCallableShader(const AStringView exportName){
    ScopedLock lock(m_mutex);
    return appendShaderRecord(
        exportName,
        ShaderTableRecordKind::Callable,
        m_callableGroupIndices,
        m_callableBuffer,
        m_callableOffset,
        m_callableCount,
        NWB_TEXT("add callable shader"),
        NWB_TEXT("callable"),
        NWB_TEXT("callable shader")
    );
}

void ShaderTable::clearMissShaders(){
    ScopedLock lock(m_mutex);
    m_missBuffer.reset();
    m_missGroupIndices.clear();
    m_missOffset = 0u;
    m_missCount = 0u;
}

void ShaderTable::clearHitShaders(){
    ScopedLock lock(m_mutex);
    m_hitBuffer.reset();
    m_hitGroupIndices.clear();
    m_hitOffset = 0u;
    m_hitCount = 0u;
}

void ShaderTable::clearCallableShaders(){
    ScopedLock lock(m_mutex);
    m_callableBuffer.reset();
    m_callableGroupIndices.clear();
    m_callableOffset = 0u;
    m_callableCount = 0u;
}

Object ShaderTable::getNativeHandle(const ObjectType objectType){
    ScopedLock lock(m_mutex);
    if(objectType == ObjectTypes::s_Buffer && m_raygenBuffer)
        return Object(m_raygenBuffer->m_buffer);
    return Object(nullptr);
}

ShaderTable::DispatchSnapshot ShaderTable::captureDispatchSnapshot()const{
    DispatchSnapshot snapshot;

    ScopedLock lock(m_mutex);
    snapshot.pipeline = m_pipeline;

    snapshot.rayGeneration.buffer = m_raygenBuffer;
    snapshot.rayGeneration.offset = m_raygenOffset;
    snapshot.rayGeneration.recordCount = m_raygenBuffer ? 1u : 0u;
    snapshot.rayGeneration.selectedGroupCount = m_raygenBuffer ? 1u : 0u;

    snapshot.miss.buffer = m_missBuffer;
    snapshot.miss.offset = m_missOffset;
    snapshot.miss.recordCount = m_missCount;
    snapshot.miss.selectedGroupCount = m_missGroupIndices.size();

    snapshot.hit.buffer = m_hitBuffer;
    snapshot.hit.offset = m_hitOffset;
    snapshot.hit.recordCount = m_hitCount;
    snapshot.hit.selectedGroupCount = m_hitGroupIndices.size();

    snapshot.callable.buffer = m_callableBuffer;
    snapshot.callable.offset = m_callableOffset;
    snapshot.callable.recordCount = m_callableCount;
    snapshot.callable.selectedGroupCount = m_callableGroupIndices.size();

    return snapshot;
}

Expected<u32> ShaderTable::findGroupIndex(
    const AStringView exportName,
    const ShaderTableRecordKind::Enum expectedKind,
    TStringView operationName,
    TStringView exportKind
)const{
    if(exportName.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} export name is empty"), operationName, exportKind);
        return MakeUnexpected(Failure{});
    }
    if(!m_pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table has no pipeline"), operationName);
        return MakeUnexpected(Failure{});
    }

    u32 groupIndex = 0u;
    u32 matchingKindCount = 0u;
    bool foundDifferentKind = false;
    for(const ShaderTableGroupMetadata& metadata : m_pipeline->m_shaderGroups){
        if(AStringView(metadata.exportName) != exportName)
            continue;
        if(metadata.kind != expectedKind){
            foundDifferentKind = true;
            continue;
        }

        groupIndex = metadata.groupIndex;
        ++matchingKindCount;
    }

    if(matchingKindCount > 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} export is ambiguous within its shader table record kind")
            , operationName
            , exportKind
        );
        return MakeUnexpected(Failure{});
    }
    if(matchingKindCount == 1u)
        return groupIndex;
    if(foundDifferentKind){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: export exists with a different shader table record kind than {}")
            , operationName
            , exportKind
        );
        return MakeUnexpected(Failure{});
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} export was not captured when the pipeline was created")
        , operationName
        , exportKind
    );
    return MakeUnexpected(Failure{});
}

Expected<ShaderTable::ShaderRecordPreflight> ShaderTable::preflightShaderRecord(
    const AStringView exportName,
    const ShaderTableRecordKind::Enum expectedKind,
    const u32 recordCount,
    TStringView operationName,
    TStringView exportKind
)const{
    const auto groupIndex = findGroupIndex(exportName, expectedKind, operationName, exportKind);
    if(!groupIndex)
        return MakeUnexpected(Failure{});
    const auto handleLayout = VulkanDetail::ComputeRayTracingHandleLayout(m_context, operationName);
    if(!handleLayout)
        return MakeUnexpected(Failure{});
    ShaderRecordPreflight preflight;
    preflight.groupIndex = *groupIndex;
    preflight.handleSize = handleLayout->handleSize;
    preflight.handleSizeAligned = handleLayout->handleSizeAligned;
    preflight.baseAlignment = handleLayout->baseAlignment;
    const u32 maxShaderGroupStride = m_context.rayTracingPipelineProperties.maxShaderGroupStride;
    if(maxShaderGroupStride == 0u || preflight.handleSizeAligned > maxShaderGroupStride){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table stride exceeds the device limit"), operationName);
        return MakeUnexpected(Failure{});
    }
    const auto recordByteSize = VulkanDetail::ComputeShaderTableByteSize(recordCount, preflight.handleSizeAligned, operationName);
    if(!recordByteSize)
        return MakeUnexpected(Failure{});
    preflight.recordByteSize = *recordByteSize;
    const auto allocationByteSize = VulkanDetail::ComputeShaderTableAllocationByteSize(preflight.recordByteSize, preflight.baseAlignment);
    if(!allocationByteSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: aligned shader table allocation size overflows"), operationName);
        return MakeUnexpected(Failure{});
    }
    preflight.allocationByteSize = *allocationByteSize;
    if(preflight.recordByteSize > Limit<usize>::s_Max || preflight.allocationByteSize > Limit<usize>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table byte size exceeds host address range"), operationName);
        return MakeUnexpected(Failure{});
    }
    if(
        preflight.groupIndex >= m_pipeline->m_shaderGroups.size()
        || m_pipeline->m_shaderGroups[preflight.groupIndex].groupIndex != preflight.groupIndex
        || m_pipeline->m_shaderGroups[preflight.groupIndex].kind != expectedKind
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: immutable shader group metadata index is invalid"), operationName);
        return MakeUnexpected(Failure{});
    }
    if(static_cast<usize>(preflight.groupIndex) > Limit<usize>::s_Max / static_cast<usize>(preflight.handleSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader group handle offset overflows"), operationName);
        return MakeUnexpected(Failure{});
    }

    preflight.handleOffset = static_cast<usize>(preflight.groupIndex) * static_cast<usize>(preflight.handleSize);
    if(
        preflight.handleOffset > m_pipeline->m_shaderGroupHandles.size()
        || static_cast<usize>(preflight.handleSize) > m_pipeline->m_shaderGroupHandles.size() - preflight.handleOffset
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader group handle range is invalid"), operationName);
        return MakeUnexpected(Failure{});
    }
    return preflight;
}

Expected<ShaderTable::ShaderTableAllocation> ShaderTable::allocateSBTBuffer(
    const ShaderRecordPreflight& preflight,
    TStringView operationName,
    TStringView recordName
){
    BufferDesc bufferDesc;
    bufferDesc.byteSize = preflight.allocationByteSize;
    bufferDesc.debugName = "SBT_Buffer";
    bufferDesc.isShaderBindingTable = true;
    bufferDesc.cpuAccess = CpuAccessMode::Write;
    bufferDesc.queueSharing = ResourceQueueSharing::GraphicsAndAsyncCompute;

    BufferHandle newBuffer = m_device.createBuffer(bufferDesc);
    if(!newBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: failed to allocate {} SBT buffer"), operationName, recordName);
        return MakeUnexpected(Failure{});
    }
    if(!m_device.isBufferReadyForGpuUse(
        newBuffer.get(),
        VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: new {} SBT buffer is not ready for GPU use")
            , operationName
            , recordName
        );
        return MakeUnexpected(Failure{});
    }
    const BufferDesc& createdDesc = newBuffer->getCreationDescription();
    if(
        !createdDesc.isShaderBindingTable
        || createdDesc.cpuAccess != CpuAccessMode::Write
        || createdDesc.byteSize < preflight.allocationByteSize
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: new {} SBT buffer does not match its construction contract")
            , operationName
            , recordName
        );
        return MakeUnexpected(Failure{});
    }

    const auto alignedOffset = VulkanDetail::ComputeShaderTableAlignedOffset(
        newBuffer->getGpuVirtualAddress(),
        createdDesc.byteSize,
        preflight.recordByteSize,
        preflight.baseAlignment
    );
    if(!alignedOffset){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: new {} SBT buffer address or aligned range is invalid")
            , operationName
            , recordName
        );
        return MakeUnexpected(Failure{});
    }
    if(*alignedOffset > Limit<usize>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: new {} SBT buffer offset exceeds host address range")
            , operationName
            , recordName
        );
        return MakeUnexpected(Failure{});
    }

    return ShaderTableAllocation{ Move(newBuffer), *alignedOffset };
}

u32 ShaderTable::appendShaderRecord(
    const AStringView exportName,
    const ShaderTableRecordKind::Enum expectedKind,
    GraphicsVector<u32>& groupIndices,
    BufferHandle& buffer,
    u64& offset,
    u32& count,
    TStringView operationName,
    TStringView recordName,
    TStringView exportKind
){
    if(count == Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table record count exceeds u32 range"), operationName);
        return s_InvalidRayTracingShaderTableRecordIndex;
    }
    if(groupIndices.size() != count){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} SBT CPU record shadow is inconsistent"), operationName, recordName);
        return s_InvalidRayTracingShaderTableRecordIndex;
    }
    if(
        (count == 0u && (buffer || offset != 0u))
        || (count != 0u && !buffer)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} SBT resource state is inconsistent"), operationName, recordName);
        return s_InvalidRayTracingShaderTableRecordIndex;
    }

    const u32 recordIndex = count;
    const u32 newCount = recordIndex + 1u;
    const auto preflight = preflightShaderRecord(exportName, expectedKind, newCount, operationName, exportKind);
    if(!preflight)
        return s_InvalidRayTracingShaderTableRecordIndex;

    GraphicsVector<u32> candidateGroupIndices(m_context.objectArena);
    candidateGroupIndices.reserve(newCount);
    for(const u32 groupIndex : groupIndices){
        if(
            groupIndex >= m_pipeline->m_shaderGroups.size()
            || m_pipeline->m_shaderGroups[groupIndex].groupIndex != groupIndex
            || m_pipeline->m_shaderGroups[groupIndex].kind != expectedKind
            || static_cast<usize>(groupIndex) > Limit<usize>::s_Max / static_cast<usize>(preflight->handleSize)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} SBT CPU record shadow contains an invalid group")
                , operationName
                , recordName
            );
            return s_InvalidRayTracingShaderTableRecordIndex;
        }

        const usize handleOffset = static_cast<usize>(groupIndex) * static_cast<usize>(preflight->handleSize);
        if(
            handleOffset > m_pipeline->m_shaderGroupHandles.size()
            || static_cast<usize>(preflight->handleSize) > m_pipeline->m_shaderGroupHandles.size() - handleOffset
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} SBT CPU record shadow handle range is invalid")
                , operationName
                , recordName
            );
            return s_InvalidRayTracingShaderTableRecordIndex;
        }
        candidateGroupIndices.push_back(groupIndex);
    }
    candidateGroupIndices.push_back(preflight->groupIndex);

    auto allocation = allocateSBTBuffer(*preflight, operationName, recordName);
    if(!allocation)
        return s_InvalidRayTracingShaderTableRecordIndex;
    BufferHandle& newBuffer = allocation->buffer;
    const u64 newOffset = allocation->offset;

    void* const newMapped = m_device.mapBuffer(*newBuffer, CpuAccessMode::Write);
    if(!newMapped){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: failed to map new {} SBT buffer"), operationName, recordName);
        return s_InvalidRayTracingShaderTableRecordIndex;
    }

    auto* const newRecordBytes = static_cast<u8*>(newMapped) + static_cast<usize>(newOffset);
    NWB_MEMSET(newRecordBytes, 0, static_cast<usize>(preflight->recordByteSize));
    usize recordOffset = 0u;
    for(const u32 groupIndex : candidateGroupIndices){
        const usize handleOffset = static_cast<usize>(groupIndex) * static_cast<usize>(preflight->handleSize);
        const u8* const handle = m_pipeline->m_shaderGroupHandles.data() + handleOffset;
        NWB_MEMCPY(newRecordBytes + recordOffset, preflight->handleSizeAligned, handle, preflight->handleSize);
        recordOffset += preflight->handleSizeAligned;
    }
    m_device.unmapBuffer(*newBuffer);

    groupIndices = Move(candidateGroupIndices);
    buffer = Move(newBuffer);
    offset = newOffset;
    count = newCount;
    return recordIndex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

