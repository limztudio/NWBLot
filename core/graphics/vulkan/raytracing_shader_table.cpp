// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_internal.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ComputeShaderTableByteSize(const u32 recordCount, const u32 handleSizeAligned, u64& outByteSize, const tchar* operation){
    if(recordCount == 0u || handleSizeAligned == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table record count or stride is invalid"), operation);
        return false;
    }
    if(static_cast<u64>(recordCount) > Limit<u64>::s_Max / static_cast<u64>(handleSizeAligned)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table size overflows"), operation);
        return false;
    }

    outByteSize = static_cast<u64>(recordCount) * static_cast<u64>(handleSizeAligned);
    return true;
}

bool ComputeShaderTableAllocationByteSize(
    const u64 recordByteSize,
    const u32 baseAlignment,
    u64& outAllocationByteSize
)noexcept{
    if(recordByteSize == 0u || baseAlignment == 0u || (baseAlignment & (baseAlignment - 1u)) != 0u)
        return false;

    const u64 alignmentPadding = static_cast<u64>(baseAlignment) - 1u;
    if(recordByteSize > Limit<u64>::s_Max - alignmentPadding)
        return false;

    outAllocationByteSize = recordByteSize + alignmentPadding;
    return true;
}

bool ComputeShaderTableAlignedOffset(
    const u64 deviceAddress,
    const u64 allocationByteSize,
    const u64 recordByteSize,
    const u32 baseAlignment,
    u64& outOffset
)noexcept{
    if(
        deviceAddress == 0u
        || allocationByteSize == 0u
        || recordByteSize == 0u
        || baseAlignment == 0u
        || (baseAlignment & (baseAlignment - 1u)) != 0u
    )
        return false;

    u64 alignedAddress = 0u;
    if(!AlignUpU64Checked(deviceAddress, static_cast<u64>(baseAlignment), alignedAddress))
        return false;
    if(alignedAddress < deviceAddress)
        return false;

    const u64 offset = alignedAddress - deviceAddress;
    if(offset > allocationByteSize || recordByteSize > allocationByteSize - offset)
        return false;

    outOffset = offset;
    return true;
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
    return RayTracingShaderTableHandle(sbt, RayTracingShaderTableHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShaderTable::ShaderTable(const VulkanContext& context, Device& device)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_missGroupIndices(context.objectArena)
    , m_hitGroupIndices(context.objectArena)
    , m_callableGroupIndices(context.objectArena)
    , m_context(context)
    , m_device(device)
{}
ShaderTable::~ShaderTable(){}

bool ShaderTable::setRayGenerationShader(const AStringView exportName){
    ScopedLock lock(m_mutex);

    ShaderRecordPreflight preflight;
    if(!preflightShaderRecord(
        exportName,
        ShaderTableRecordKind::RayGeneration,
        1u,
        preflight,
        NWB_TEXT("set ray generation shader"),
        NWB_TEXT("ray generation")
    ))
        return false;

    BufferHandle newBuffer;
    u64 newOffset = 0u;
    if(!allocateSBTBuffer(preflight, newBuffer, newOffset, NWB_TEXT("set ray generation shader"), NWB_TEXT("ray generation")))
        return false;

#if !defined(NWB_FINAL)
    if(m_rejectNextNewBufferMapForTesting){
        m_rejectNextNewBufferMapForTesting = false;
        return false;
    }
#endif

    void* const mapped = m_device.mapBuffer(newBuffer.get(), CpuAccessMode::Write);
    if(!mapped){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to set ray generation shader: failed to map new SBT buffer"));
        return false;
    }

    auto* const recordBytes = static_cast<u8*>(mapped) + static_cast<usize>(newOffset);
    NWB_MEMSET(recordBytes, 0, static_cast<usize>(preflight.recordByteSize));
    const u8* const handle = m_pipeline->m_shaderGroupHandles.data() + preflight.handleOffset;
    NWB_MEMCPY(recordBytes, preflight.handleSizeAligned, handle, preflight.handleSize);
    m_device.unmapBuffer(newBuffer.get());

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
    if(objectType == ObjectTypes::VK_Buffer && m_raygenBuffer)
        return Object(m_raygenBuffer->m_buffer);
    return Object(nullptr);
}

#if !defined(NWB_FINAL)
void ShaderTable::rejectNextBufferAllocationForTesting(){
    ScopedLock lock(m_mutex);
    m_rejectNextBufferAllocationForTesting = true;
}

void ShaderTable::rejectNextNewBufferMapForTesting(){
    ScopedLock lock(m_mutex);
    m_rejectNextNewBufferMapForTesting = true;
}
#endif

bool ShaderTable::findGroupIndex(
    const AStringView exportName,
    const ShaderTableRecordKind::Enum expectedKind,
    u32& outGroupIndex,
    const tchar* operationName,
    const tchar* exportKind
)const{
    if(exportName.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} export name is empty"), operationName, exportKind);
        return false;
    }
    if(!m_pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table has no pipeline"), operationName);
        return false;
    }

    u32 matchingKindCount = 0u;
    bool foundDifferentKind = false;
    for(const ShaderTableGroupMetadata& metadata : m_pipeline->m_shaderGroups){
        if(AStringView(metadata.exportName) != exportName)
            continue;
        if(metadata.kind != expectedKind){
            foundDifferentKind = true;
            continue;
        }

        outGroupIndex = metadata.groupIndex;
        ++matchingKindCount;
    }

    if(matchingKindCount > 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} export is ambiguous within its shader table record kind")
            , operationName
            , exportKind
        );
        return false;
    }
    if(matchingKindCount == 1u)
        return true;
    if(foundDifferentKind){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: export exists with a different shader table record kind than {}")
            , operationName
            , exportKind
        );
        return false;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} export was not captured when the pipeline was created")
        , operationName
        , exportKind
    );
    return false;
}

bool ShaderTable::preflightShaderRecord(
    const AStringView exportName,
    const ShaderTableRecordKind::Enum expectedKind,
    const u32 recordCount,
    ShaderRecordPreflight& outPreflight,
    const tchar* operationName,
    const tchar* exportKind
)const{
    if(!findGroupIndex(exportName, expectedKind, outPreflight.groupIndex, operationName, exportKind))
        return false;

    if(!VulkanDetail::ComputeRayTracingHandleLayout(
        m_context,
        outPreflight.handleSize,
        outPreflight.handleSizeAligned,
        outPreflight.baseAlignment,
        operationName
    ))
        return false;
    const u32 maxShaderGroupStride = m_context.rayTracingPipelineProperties.maxShaderGroupStride;
    if(maxShaderGroupStride == 0u || outPreflight.handleSizeAligned > maxShaderGroupStride){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table stride exceeds the device limit"), operationName);
        return false;
    }
    if(!VulkanDetail::ComputeShaderTableByteSize(
        recordCount,
        outPreflight.handleSizeAligned,
        outPreflight.recordByteSize,
        operationName
    ))
        return false;
    if(!VulkanDetail::ComputeShaderTableAllocationByteSize(
        outPreflight.recordByteSize,
        outPreflight.baseAlignment,
        outPreflight.allocationByteSize
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: aligned shader table allocation size overflows"), operationName);
        return false;
    }
    if(outPreflight.recordByteSize > Limit<usize>::s_Max || outPreflight.allocationByteSize > Limit<usize>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader table byte size exceeds host address range"), operationName);
        return false;
    }
    if(
        outPreflight.groupIndex >= m_pipeline->m_shaderGroups.size()
        || m_pipeline->m_shaderGroups[outPreflight.groupIndex].groupIndex != outPreflight.groupIndex
        || m_pipeline->m_shaderGroups[outPreflight.groupIndex].kind != expectedKind
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: immutable shader group metadata index is invalid"), operationName);
        return false;
    }
    if(static_cast<usize>(outPreflight.groupIndex) > Limit<usize>::s_Max / static_cast<usize>(outPreflight.handleSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader group handle offset overflows"), operationName);
        return false;
    }

    outPreflight.handleOffset = static_cast<usize>(outPreflight.groupIndex) * static_cast<usize>(outPreflight.handleSize);
    if(
        outPreflight.handleOffset > m_pipeline->m_shaderGroupHandles.size()
        || static_cast<usize>(outPreflight.handleSize) > m_pipeline->m_shaderGroupHandles.size() - outPreflight.handleOffset
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: shader group handle range is invalid"), operationName);
        return false;
    }
    return true;
}

bool ShaderTable::allocateSBTBuffer(
    const ShaderRecordPreflight& preflight,
    BufferHandle& outBuffer,
    u64& outOffset,
    const tchar* operationName,
    const tchar* recordName
){
#if !defined(NWB_FINAL)
    if(m_rejectNextBufferAllocationForTesting){
        m_rejectNextBufferAllocationForTesting = false;
        return false;
    }
#endif

    BufferDesc bufferDesc;
    bufferDesc.byteSize = preflight.allocationByteSize;
    bufferDesc.debugName = "SBT_Buffer";
    bufferDesc.isShaderBindingTable = true;
    bufferDesc.cpuAccess = CpuAccessMode::Write;

    BufferHandle newBuffer = m_device.createBuffer(bufferDesc);
    if(!newBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: failed to allocate {} SBT buffer"), operationName, recordName);
        return false;
    }
    if(!m_device.isBufferReadyForGpuUse(newBuffer.get())){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: new {} SBT buffer is not ready for GPU use")
            , operationName
            , recordName
        );
        return false;
    }
    const BufferDesc& createdDesc = newBuffer->getDescription();
    if(
        !createdDesc.isShaderBindingTable
        || createdDesc.cpuAccess != CpuAccessMode::Write
        || createdDesc.byteSize < preflight.allocationByteSize
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: new {} SBT buffer does not match its construction contract")
            , operationName
            , recordName
        );
        return false;
    }

    u64 alignedOffset = 0u;
    if(!VulkanDetail::ComputeShaderTableAlignedOffset(
        newBuffer->getGpuVirtualAddress(),
        createdDesc.byteSize,
        preflight.recordByteSize,
        preflight.baseAlignment,
        alignedOffset
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: new {} SBT buffer address or aligned range is invalid")
            , operationName
            , recordName
        );
        return false;
    }
    if(alignedOffset > Limit<usize>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: new {} SBT buffer offset exceeds host address range")
            , operationName
            , recordName
        );
        return false;
    }

    outBuffer = Move(newBuffer);
    outOffset = alignedOffset;
    return true;
}

u32 ShaderTable::appendShaderRecord(
    const AStringView exportName,
    const ShaderTableRecordKind::Enum expectedKind,
    GraphicsVector<u32>& groupIndices,
    BufferHandle& buffer,
    u64& offset,
    u32& count,
    const tchar* operationName,
    const tchar* recordName,
    const tchar* exportKind
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
    ShaderRecordPreflight preflight;
    if(!preflightShaderRecord(exportName, expectedKind, newCount, preflight, operationName, exportKind))
        return s_InvalidRayTracingShaderTableRecordIndex;

    GraphicsVector<u32> candidateGroupIndices(m_context.objectArena);
    candidateGroupIndices.reserve(newCount);
    for(const u32 groupIndex : groupIndices){
        if(
            groupIndex >= m_pipeline->m_shaderGroups.size()
            || m_pipeline->m_shaderGroups[groupIndex].groupIndex != groupIndex
            || m_pipeline->m_shaderGroups[groupIndex].kind != expectedKind
            || static_cast<usize>(groupIndex) > Limit<usize>::s_Max / static_cast<usize>(preflight.handleSize)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} SBT CPU record shadow contains an invalid group")
                , operationName
                , recordName
            );
            return s_InvalidRayTracingShaderTableRecordIndex;
        }

        const usize handleOffset = static_cast<usize>(groupIndex) * static_cast<usize>(preflight.handleSize);
        if(
            handleOffset > m_pipeline->m_shaderGroupHandles.size()
            || static_cast<usize>(preflight.handleSize) > m_pipeline->m_shaderGroupHandles.size() - handleOffset
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} SBT CPU record shadow handle range is invalid")
                , operationName
                , recordName
            );
            return s_InvalidRayTracingShaderTableRecordIndex;
        }
        candidateGroupIndices.push_back(groupIndex);
    }
    candidateGroupIndices.push_back(preflight.groupIndex);

    BufferHandle newBuffer;
    u64 newOffset = 0u;
    if(!allocateSBTBuffer(preflight, newBuffer, newOffset, operationName, recordName))
        return s_InvalidRayTracingShaderTableRecordIndex;

#if !defined(NWB_FINAL)
    if(m_rejectNextNewBufferMapForTesting){
        m_rejectNextNewBufferMapForTesting = false;
        return s_InvalidRayTracingShaderTableRecordIndex;
    }
#endif

    void* const newMapped = m_device.mapBuffer(newBuffer.get(), CpuAccessMode::Write);
    if(!newMapped){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: failed to map new {} SBT buffer"), operationName, recordName);
        return s_InvalidRayTracingShaderTableRecordIndex;
    }

    auto* const newRecordBytes = static_cast<u8*>(newMapped) + static_cast<usize>(newOffset);
    NWB_MEMSET(newRecordBytes, 0, static_cast<usize>(preflight.recordByteSize));
    usize recordOffset = 0u;
    for(const u32 groupIndex : candidateGroupIndices){
        const usize handleOffset = static_cast<usize>(groupIndex) * static_cast<usize>(preflight.handleSize);
        const u8* const handle = m_pipeline->m_shaderGroupHandles.data() + handleOffset;
        NWB_MEMCPY(newRecordBytes + recordOffset, preflight.handleSizeAligned, handle, preflight.handleSize);
        recordOffset += preflight.handleSizeAligned;
    }
    m_device.unmapBuffer(newBuffer.get());

    groupIndices = Move(candidateGroupIndices);
    buffer = Move(newBuffer);
    offset = newOffset;
    count = newCount;
    return recordIndex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

