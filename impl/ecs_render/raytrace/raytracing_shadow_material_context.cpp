// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_system.h"

#include <impl/ecs_render/material/sampled_texture_collection.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/rt_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::clearPreparedShadowMaterialContext()noexcept{
    m_preparedShadowInstanceMaterialBytes.clear();
    m_preparedShadowInstanceBytes.clear();
    m_preparedShadowMaterialTypedBytes.clear();
    m_preparedShadowInstanceMaterialBuffer = nullptr;
    m_preparedShadowInstanceBuffer = nullptr;
    m_preparedShadowMaterialTypedBuffer = nullptr;
    m_preparedShadowInstanceMaterialHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_preparedShadowInstanceHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_preparedShadowMaterialTypedHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_preparedShadowInstanceMaterialCount = 0u;
    m_preparedShadowInstanceCount = 0u;
    m_preparedShadowMaterialTypedUploadBytes = 0u;
    m_preparedShadowInstanceMaterialCapacity = 0u;
    m_preparedShadowInstanceCapacity = 0u;
    m_preparedShadowMaterialTypedCapacity = 0u;
    m_preparedShadowMaterialContextHash = 0u;
    m_preparedShadowMaterialContextRoute = PreparedShadowMaterialContextRoute::None;
    m_preparedShadowMaterialContextStatic = false;
    m_preparedShadowMaterialContextReady = false;
    m_preparedShadowMaterialContextUploadRequired = false;
    clearPreparedSceneSwBvhTraversal();
}

bool RendererRayTracingSystem::capturePreparedShadowMaterialContext(
    const PreparedShadowMaterialContextRoute route,
    const bool staticScene,
    const u64 hash,
    const void* const instanceMaterialData,
    const usize instanceMaterialCount,
    const usize instanceMaterialByteCount,
    const void* const instanceData,
    const usize instanceCount,
    const usize instanceByteCount,
    const void* const materialTypedData,
    const usize materialTypedByteCount
){
    const auto& state = m_rayTracingState;
    const bool validStorage =
        state.m_shadowInstanceMaterialBuffer
        && state.m_shadowInstanceBuffer
        && state.m_shadowMaterialTypedBuffer
        && state.m_shadowInstanceMaterialCapacity >= instanceMaterialCount
        && state.m_shadowInstanceCapacity >= instanceCount
        && state.m_shadowMaterialTypedCapacity >= materialTypedByteCount
        && state.m_shadowInstanceMaterialHeapHandle.valid()
        && state.m_shadowInstanceMaterialHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && state.m_shadowInstanceHeapHandle.valid()
        && state.m_shadowInstanceHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && state.m_shadowMaterialTypedHeapHandle.valid()
        && state.m_shadowMaterialTypedHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
    ;
    if(
        route == PreparedShadowMaterialContextRoute::None
        || !instanceMaterialData
        || !instanceData
        || !materialTypedData
        || instanceMaterialCount == 0u
        || instanceCount == 0u
        || instanceMaterialByteCount == 0u
        || instanceByteCount == 0u
        || materialTypedByteCount == 0u
        || !validStorage
    )
        return false;

    clearPreparedShadowMaterialContext();
    m_preparedShadowInstanceMaterialBytes.resize(instanceMaterialByteCount);
    m_preparedShadowInstanceBytes.resize(instanceByteCount);
    m_preparedShadowMaterialTypedBytes.resize(materialTypedByteCount);
    NWB_MEMCPY(
        m_preparedShadowInstanceMaterialBytes.data(),
        m_preparedShadowInstanceMaterialBytes.size(),
        instanceMaterialData,
        instanceMaterialByteCount
    );
    NWB_MEMCPY(
        m_preparedShadowInstanceBytes.data(),
        m_preparedShadowInstanceBytes.size(),
        instanceData,
        instanceByteCount
    );
    NWB_MEMCPY(
        m_preparedShadowMaterialTypedBytes.data(),
        m_preparedShadowMaterialTypedBytes.size(),
        materialTypedData,
        materialTypedByteCount
    );
    m_preparedShadowInstanceMaterialBuffer = state.m_shadowInstanceMaterialBuffer;
    m_preparedShadowInstanceBuffer = state.m_shadowInstanceBuffer;
    m_preparedShadowMaterialTypedBuffer = state.m_shadowMaterialTypedBuffer;
    m_preparedShadowInstanceMaterialHeapHandle = state.m_shadowInstanceMaterialHeapHandle;
    m_preparedShadowInstanceHeapHandle = state.m_shadowInstanceHeapHandle;
    m_preparedShadowMaterialTypedHeapHandle = state.m_shadowMaterialTypedHeapHandle;
    m_preparedShadowInstanceMaterialCount = instanceMaterialCount;
    m_preparedShadowInstanceCount = instanceCount;
    m_preparedShadowMaterialTypedUploadBytes = materialTypedByteCount;
    m_preparedShadowInstanceMaterialCapacity = state.m_shadowInstanceMaterialCapacity;
    m_preparedShadowInstanceCapacity = state.m_shadowInstanceCapacity;
    m_preparedShadowMaterialTypedCapacity = state.m_shadowMaterialTypedCapacity;
    m_preparedShadowMaterialContextHash = hash;
    m_preparedShadowMaterialContextRoute = route;
    m_preparedShadowMaterialContextStatic = staticScene;
    m_preparedShadowMaterialContextReady = true;
    m_preparedShadowMaterialContextUploadRequired = true;
    return true;
}

bool RendererRayTracingSystem::capturePreparedShadowMaterialContextCacheReuse(
    const u64 hash,
    const usize instanceMaterialCount,
    const usize instanceCount,
    const usize materialTypedByteCount
){
    clearPreparedShadowMaterialContext();
    if(instanceMaterialCount == 0u || instanceCount == 0u || materialTypedByteCount == 0u)
        return false;

    const auto& state = m_rayTracingState;
    const bool validStorage =
        state.m_shadowInstanceMaterialBuffer
        && state.m_shadowInstanceBuffer
        && state.m_shadowMaterialTypedBuffer
        && state.m_shadowInstanceMaterialCapacity >= instanceMaterialCount
        && state.m_shadowInstanceCapacity >= instanceCount
        && state.m_shadowMaterialTypedCapacity >= materialTypedByteCount
        && state.m_shadowInstanceMaterialHeapHandle.valid()
        && state.m_shadowInstanceMaterialHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && state.m_shadowInstanceHeapHandle.valid()
        && state.m_shadowInstanceHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && state.m_shadowMaterialTypedHeapHandle.valid()
        && state.m_shadowMaterialTypedHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
    ;
    if(
        !state.m_swShadowMaterialContextHashValid
        || state.m_swShadowMaterialContextHash != hash
        || !validStorage
    )
        return false;

    m_preparedShadowInstanceMaterialBuffer = state.m_shadowInstanceMaterialBuffer;
    m_preparedShadowInstanceBuffer = state.m_shadowInstanceBuffer;
    m_preparedShadowMaterialTypedBuffer = state.m_shadowMaterialTypedBuffer;
    m_preparedShadowInstanceMaterialHeapHandle = state.m_shadowInstanceMaterialHeapHandle;
    m_preparedShadowInstanceHeapHandle = state.m_shadowInstanceHeapHandle;
    m_preparedShadowMaterialTypedHeapHandle = state.m_shadowMaterialTypedHeapHandle;
    m_preparedShadowInstanceMaterialCount = instanceMaterialCount;
    m_preparedShadowInstanceCount = instanceCount;
    m_preparedShadowMaterialTypedUploadBytes = materialTypedByteCount;
    m_preparedShadowInstanceMaterialCapacity = state.m_shadowInstanceMaterialCapacity;
    m_preparedShadowInstanceCapacity = state.m_shadowInstanceCapacity;
    m_preparedShadowMaterialTypedCapacity = state.m_shadowMaterialTypedCapacity;
    m_preparedShadowMaterialContextHash = hash;
    m_preparedShadowMaterialContextRoute = PreparedShadowMaterialContextRoute::Software;
    m_preparedShadowMaterialContextStatic = true;
    m_preparedShadowMaterialContextReady = true;
    m_preparedShadowMaterialContextUploadRequired = false;
    return true;
}

bool RendererRayTracingSystem::matchesPreparedShadowMaterialContext(
    const PreparedShadowMaterialContextRoute route,
    const bool staticScene,
    const u64 hash,
    const void* const instanceMaterialData,
    const usize instanceMaterialCount,
    const usize instanceMaterialByteCount,
    const void* const instanceData,
    const usize instanceCount,
    const usize instanceByteCount,
    const void* const materialTypedData,
    const usize materialTypedByteCount
)const{
    const auto& state = m_rayTracingState;
    if(
        !m_preparedShadowMaterialContextReady
        || !m_preparedShadowMaterialContextUploadRequired
        || m_preparedShadowMaterialContextRoute != route
        || m_preparedShadowMaterialContextStatic != staticScene
        || m_preparedShadowMaterialContextHash != hash
        || !instanceMaterialData
        || !instanceData
        || !materialTypedData
        || m_preparedShadowInstanceMaterialCount != instanceMaterialCount
        || m_preparedShadowInstanceCount != instanceCount
        || m_preparedShadowMaterialTypedUploadBytes != materialTypedByteCount
        || m_preparedShadowInstanceMaterialBytes.size() != instanceMaterialByteCount
        || m_preparedShadowInstanceBytes.size() != instanceByteCount
        || m_preparedShadowMaterialTypedBytes.size() != materialTypedByteCount
        || state.m_shadowInstanceMaterialBuffer.get() != m_preparedShadowInstanceMaterialBuffer.get()
        || state.m_shadowInstanceBuffer.get() != m_preparedShadowInstanceBuffer.get()
        || state.m_shadowMaterialTypedBuffer.get() != m_preparedShadowMaterialTypedBuffer.get()
        || state.m_shadowInstanceMaterialCapacity != m_preparedShadowInstanceMaterialCapacity
        || state.m_shadowInstanceCapacity != m_preparedShadowInstanceCapacity
        || state.m_shadowMaterialTypedCapacity != m_preparedShadowMaterialTypedCapacity
        || state.m_shadowInstanceMaterialHeapHandle != m_preparedShadowInstanceMaterialHeapHandle
        || state.m_shadowInstanceHeapHandle != m_preparedShadowInstanceHeapHandle
        || state.m_shadowMaterialTypedHeapHandle != m_preparedShadowMaterialTypedHeapHandle
        || !state.m_shadowInstanceMaterialHeapHandle.valid()
        || state.m_shadowInstanceMaterialHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
        || !state.m_shadowInstanceHeapHandle.valid()
        || state.m_shadowInstanceHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
        || !state.m_shadowMaterialTypedHeapHandle.valid()
        || state.m_shadowMaterialTypedHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
    )
        return false;
    return
        NWB_MEMCMP(
            m_preparedShadowInstanceMaterialBytes.data(),
            instanceMaterialData,
            instanceMaterialByteCount
        ) == 0
        && NWB_MEMCMP(
            m_preparedShadowInstanceBytes.data(),
            instanceData,
            instanceByteCount
        ) == 0
        && NWB_MEMCMP(
            m_preparedShadowMaterialTypedBytes.data(),
            materialTypedData,
            materialTypedByteCount
        ) == 0
    ;
}

bool RendererRayTracingSystem::retainPreparedShadowMaterialContextUploads(
    Core::GpuTaskGraph& graph,
    Core::GpuUploadBlobId& outInstanceMaterialBlob,
    Core::GpuUploadBlobId& outInstanceBlob,
    Core::GpuUploadBlobId& outMaterialTypedBlob
)const{
    outInstanceMaterialBlob = {};
    outInstanceBlob = {};
    outMaterialTypedBlob = {};
    if(!m_preparedShadowMaterialContextReady)
        return true;

    const auto& state = m_rayTracingState;
    if(
        m_preparedShadowMaterialContextRoute == PreparedShadowMaterialContextRoute::None
        || m_preparedShadowInstanceMaterialCount == 0u
        || m_preparedShadowInstanceCount == 0u
        || m_preparedShadowMaterialTypedUploadBytes == 0u
        || m_preparedShadowInstanceMaterialCapacity < m_preparedShadowInstanceMaterialCount
        || m_preparedShadowInstanceCapacity < m_preparedShadowInstanceCount
        || m_preparedShadowMaterialTypedCapacity < m_preparedShadowMaterialTypedUploadBytes
        || state.m_shadowInstanceMaterialBuffer.get() != m_preparedShadowInstanceMaterialBuffer.get()
        || state.m_shadowInstanceBuffer.get() != m_preparedShadowInstanceBuffer.get()
        || state.m_shadowMaterialTypedBuffer.get() != m_preparedShadowMaterialTypedBuffer.get()
        || state.m_shadowInstanceMaterialCapacity != m_preparedShadowInstanceMaterialCapacity
        || state.m_shadowInstanceCapacity != m_preparedShadowInstanceCapacity
        || state.m_shadowMaterialTypedCapacity != m_preparedShadowMaterialTypedCapacity
        || state.m_shadowInstanceMaterialHeapHandle != m_preparedShadowInstanceMaterialHeapHandle
        || state.m_shadowInstanceHeapHandle != m_preparedShadowInstanceHeapHandle
        || state.m_shadowMaterialTypedHeapHandle != m_preparedShadowMaterialTypedHeapHandle
        || !state.m_shadowInstanceMaterialHeapHandle.valid()
        || state.m_shadowInstanceMaterialHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
        || !state.m_shadowInstanceHeapHandle.valid()
        || state.m_shadowInstanceHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
        || !state.m_shadowMaterialTypedHeapHandle.valid()
        || state.m_shadowMaterialTypedHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frozen shadow material-context identity no longer matches preflight storage"));
        return false;
    }
    if(!m_preparedShadowMaterialContextUploadRequired){
        if(
            m_preparedShadowMaterialContextRoute != PreparedShadowMaterialContextRoute::Software
            || !m_preparedShadowMaterialContextStatic
            || !state.m_swShadowMaterialContextHashValid
            || state.m_swShadowMaterialContextHash != m_preparedShadowMaterialContextHash
            || state.m_sceneBvhInstanceCount != m_preparedShadowInstanceMaterialCount
            || state.m_sceneBvhInstanceCount != m_preparedShadowInstanceCount
            || !m_preparedShadowInstanceMaterialBytes.empty()
            || !m_preparedShadowInstanceBytes.empty()
            || !m_preparedShadowMaterialTypedBytes.empty()
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frozen software material-context cache identity is no longer accepted"));
            return false;
        }
        return true;
    }
    if(
        m_preparedShadowInstanceMaterialBytes.empty()
        || m_preparedShadowInstanceBytes.empty()
        || m_preparedShadowMaterialTypedBytes.empty()
        || m_preparedShadowInstanceMaterialBytes.size()
            != m_preparedShadowInstanceMaterialCount * sizeof(NwbRtInstanceMaterialGpu)
        || m_preparedShadowInstanceBytes.size() != m_preparedShadowInstanceCount * sizeof(InstanceGpuData)
        || m_preparedShadowMaterialTypedBytes.size() != m_preparedShadowMaterialTypedUploadBytes
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frozen shadow material-context payload is incomplete"));
        return false;
    }

    outInstanceMaterialBlob = graph.copyUploadData(
        m_preparedShadowInstanceMaterialBytes.data(),
        m_preparedShadowInstanceMaterialBytes.size(),
        alignof(NwbRtInstanceMaterialGpu)
    );
    outInstanceBlob = graph.copyUploadData(
        m_preparedShadowInstanceBytes.data(),
        m_preparedShadowInstanceBytes.size(),
        alignof(InstanceGpuData)
    );
    outMaterialTypedBlob = graph.copyUploadData(
        m_preparedShadowMaterialTypedBytes.data(),
        m_preparedShadowMaterialTypedBytes.size(),
        alignof(u32)
    );
    return outInstanceMaterialBlob.valid() && outInstanceBlob.valid() && outMaterialTypedBlob.valid();
}

void RendererRayTracingSystem::confirmPreparedShadowMaterialContextUploads()noexcept{
    if(m_preparedShadowMaterialContextReady && m_preparedShadowMaterialContextUploadRequired){
        if(m_preparedShadowMaterialContextRoute == PreparedShadowMaterialContextRoute::Hardware){
            m_rayTracingState.m_hwShadowMaterialContextHash = m_preparedShadowMaterialContextHash;
            m_rayTracingState.m_hwShadowMaterialContextHashValid = m_preparedShadowMaterialContextStatic;
            m_rayTracingState.m_swShadowMaterialContextHashValid = false;
        }
        else if(m_preparedShadowMaterialContextRoute == PreparedShadowMaterialContextRoute::Software){
            m_rayTracingState.m_swShadowMaterialContextHash = m_preparedShadowMaterialContextHash;
            m_rayTracingState.m_swShadowMaterialContextHashValid = m_preparedShadowMaterialContextStatic;
            m_rayTracingState.m_hwShadowMaterialContextHashValid = false;
        }
        clearPreparedShadowMaterialContext();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

