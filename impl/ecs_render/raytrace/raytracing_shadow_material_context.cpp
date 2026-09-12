// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_system.h"

#include <impl/ecs_render/material/sampled_texture_collection.h>

#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/rt_private.h>

#include <global/overflow.h>


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

void RendererRayTracingSystem::clearPreparedHybridHardwareMaterialContextFallback()noexcept{
    m_preparedHybridHardwareFallbackBytes.clear();
    m_preparedHybridHardwareFallbackInstanceMaterialBuffer = nullptr;
    m_preparedHybridHardwareFallbackInstanceBuffer = nullptr;
    m_preparedHybridHardwareFallbackMaterialTypedBuffer = nullptr;
    m_preparedHybridHardwareFallbackInstanceMaterialHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_preparedHybridHardwareFallbackInstanceHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_preparedHybridHardwareFallbackMaterialTypedHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_preparedHybridHardwareFallbackInstanceMaterialByteCount = 0u;
    m_preparedHybridHardwareFallbackInstanceByteCount = 0u;
    m_preparedHybridHardwareFallbackMaterialTypedByteCount = 0u;
    m_preparedHybridHardwareFallbackInstanceMaterialCapacity = 0u;
    m_preparedHybridHardwareFallbackInstanceCapacity = 0u;
    m_preparedHybridHardwareFallbackMaterialTypedCapacity = 0u;
    m_preparedHybridHardwareFallbackMaterialContextHash = 0u;
    m_preparedHybridHardwareFallbackRendererMutationVersion = 0u;
    m_preparedHybridHardwareFallbackTransformMutationVersion = 0u;
    m_preparedHybridHardwareFallbackMaterialMutationVersion = 0u;
    m_preparedHybridHardwareFallbackStatic = false;
    m_preparedHybridHardwareFallbackReady = false;
    m_preparedHybridHardwareFallbackRecorded = false;
}

bool RendererRayTracingSystem::capturePreparedHybridHardwareMaterialContextFallback(){
    clearPreparedHybridHardwareMaterialContextFallback();
    if(
        !m_preparedShadowMaterialContextReady
        || m_preparedShadowMaterialContextRoute != PreparedShadowMaterialContextRoute::Hardware
        || m_preparedShadowInstanceMaterialBytes.empty()
        || m_preparedShadowInstanceBytes.empty()
        || m_preparedShadowMaterialTypedBytes.empty()
    )
        return false;

    const usize instanceMaterialByteCount = m_preparedShadowInstanceMaterialBytes.size();
    const usize instanceByteCount = m_preparedShadowInstanceBytes.size();
    const usize materialTypedByteCount = m_preparedShadowMaterialTypedBytes.size();
    if(
        instanceMaterialByteCount > Limit<usize>::s_Max - instanceByteCount
        || instanceMaterialByteCount + instanceByteCount > Limit<usize>::s_Max - materialTypedByteCount
        || instanceMaterialByteCount % sizeof(NwbRtInstanceMaterialGpu) != 0u
        || instanceByteCount % sizeof(InstanceGpuData) != 0u
    )
        return false;
    const auto& state = m_rayTracingState;
    const usize instanceMaterialCount = instanceMaterialByteCount / sizeof(NwbRtInstanceMaterialGpu);
    const usize instanceCount = instanceByteCount / sizeof(InstanceGpuData);
    if(
        !state.m_shadowInstanceMaterialBuffer
        || !state.m_shadowInstanceBuffer
        || !state.m_shadowMaterialTypedBuffer
        || state.m_shadowInstanceMaterialCapacity < instanceMaterialCount
        || state.m_shadowInstanceCapacity < instanceCount
        || state.m_shadowMaterialTypedCapacity < materialTypedByteCount
        || !state.m_shadowInstanceMaterialHeapHandle.valid()
        || state.m_shadowInstanceMaterialHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
        || !state.m_shadowInstanceHeapHandle.valid()
        || state.m_shadowInstanceHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
        || !state.m_shadowMaterialTypedHeapHandle.valid()
        || state.m_shadowMaterialTypedHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
    )
        return false;
    m_preparedHybridHardwareFallbackBytes.resize(
        instanceMaterialByteCount + instanceByteCount + materialTypedByteCount
    );
    u8* const destination = m_preparedHybridHardwareFallbackBytes.data();
    NWB_MEMCPY(
        destination,
        m_preparedHybridHardwareFallbackBytes.size(),
        m_preparedShadowInstanceMaterialBytes.data(),
        instanceMaterialByteCount
    );
    NWB_MEMCPY(
        destination + instanceMaterialByteCount,
        m_preparedHybridHardwareFallbackBytes.size() - instanceMaterialByteCount,
        m_preparedShadowInstanceBytes.data(),
        instanceByteCount
    );
    NWB_MEMCPY(
        destination + instanceMaterialByteCount + instanceByteCount,
        m_preparedHybridHardwareFallbackBytes.size() - instanceMaterialByteCount - instanceByteCount,
        m_preparedShadowMaterialTypedBytes.data(),
        materialTypedByteCount
    );
    m_preparedHybridHardwareFallbackInstanceMaterialBuffer = state.m_shadowInstanceMaterialBuffer;
    m_preparedHybridHardwareFallbackInstanceBuffer = state.m_shadowInstanceBuffer;
    m_preparedHybridHardwareFallbackMaterialTypedBuffer = state.m_shadowMaterialTypedBuffer;
    m_preparedHybridHardwareFallbackInstanceMaterialHeapHandle = state.m_shadowInstanceMaterialHeapHandle;
    m_preparedHybridHardwareFallbackInstanceHeapHandle = state.m_shadowInstanceHeapHandle;
    m_preparedHybridHardwareFallbackMaterialTypedHeapHandle = state.m_shadowMaterialTypedHeapHandle;
    m_preparedHybridHardwareFallbackInstanceMaterialByteCount = instanceMaterialByteCount;
    m_preparedHybridHardwareFallbackInstanceByteCount = instanceByteCount;
    m_preparedHybridHardwareFallbackMaterialTypedByteCount = materialTypedByteCount;
    m_preparedHybridHardwareFallbackInstanceMaterialCapacity = state.m_shadowInstanceMaterialCapacity;
    m_preparedHybridHardwareFallbackInstanceCapacity = state.m_shadowInstanceCapacity;
    m_preparedHybridHardwareFallbackMaterialTypedCapacity = state.m_shadowMaterialTypedCapacity;
    m_preparedHybridHardwareFallbackMaterialContextHash = m_preparedShadowMaterialContextHash;
    m_preparedHybridHardwareFallbackRendererMutationVersion = m_world.componentMutationVersion<RendererComponent>();
    m_preparedHybridHardwareFallbackTransformMutationVersion = m_world.componentMutationVersion<NWB::Impl::Scene::TransformComponent>();
    m_preparedHybridHardwareFallbackMaterialMutationVersion = m_world.componentMutationVersion<MaterialInstanceComponent>();
    m_preparedHybridHardwareFallbackStatic = m_preparedShadowMaterialContextStatic;
    m_preparedHybridHardwareFallbackReady = true;
    return true;
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

bool RendererRayTracingSystem::retainPreparedHybridHardwareMaterialContextFallbackUploads(
    Core::GpuTaskGraph& graph,
    Core::GpuUploadBlobId& outInstanceMaterialBlob,
    Core::GpuUploadBlobId& outInstanceBlob,
    Core::GpuUploadBlobId& outMaterialTypedBlob
)const{
    outInstanceMaterialBlob = {};
    outInstanceBlob = {};
    outMaterialTypedBlob = {};
    if(!m_preparedHybridHardwareFallbackReady)
        return false;

    const auto& state = m_rayTracingState;
    const usize instanceMaterialByteCount = m_preparedHybridHardwareFallbackInstanceMaterialByteCount;
    const usize instanceByteCount = m_preparedHybridHardwareFallbackInstanceByteCount;
    const usize materialTypedByteCount = m_preparedHybridHardwareFallbackMaterialTypedByteCount;
    if(
        instanceMaterialByteCount == 0u
        || instanceByteCount == 0u
        || materialTypedByteCount == 0u
        || instanceMaterialByteCount % sizeof(NwbRtInstanceMaterialGpu) != 0u
        || instanceByteCount % sizeof(InstanceGpuData) != 0u
        || instanceMaterialByteCount > Limit<usize>::s_Max - instanceByteCount
        || instanceMaterialByteCount + instanceByteCount > Limit<usize>::s_Max - materialTypedByteCount
        || m_preparedHybridHardwareFallbackBytes.size()
            != instanceMaterialByteCount + instanceByteCount + materialTypedByteCount
        || state.m_shadowInstanceMaterialBuffer.get() != m_preparedHybridHardwareFallbackInstanceMaterialBuffer.get()
        || state.m_shadowInstanceBuffer.get() != m_preparedHybridHardwareFallbackInstanceBuffer.get()
        || state.m_shadowMaterialTypedBuffer.get() != m_preparedHybridHardwareFallbackMaterialTypedBuffer.get()
        || state.m_shadowInstanceMaterialCapacity != m_preparedHybridHardwareFallbackInstanceMaterialCapacity
        || state.m_shadowInstanceCapacity != m_preparedHybridHardwareFallbackInstanceCapacity
        || state.m_shadowMaterialTypedCapacity != m_preparedHybridHardwareFallbackMaterialTypedCapacity
        || state.m_shadowInstanceMaterialHeapHandle != m_preparedHybridHardwareFallbackInstanceMaterialHeapHandle
        || state.m_shadowInstanceHeapHandle != m_preparedHybridHardwareFallbackInstanceHeapHandle
        || state.m_shadowMaterialTypedHeapHandle != m_preparedHybridHardwareFallbackMaterialTypedHeapHandle
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen hybrid hardware material fallback could not retain graph uploads"));
        return false;
    }

    const u8* const bytes = m_preparedHybridHardwareFallbackBytes.data();
    outInstanceMaterialBlob = graph.copyUploadData(
        bytes,
        instanceMaterialByteCount,
        alignof(NwbRtInstanceMaterialGpu)
    );
    outInstanceBlob = graph.copyUploadData(
        bytes + instanceMaterialByteCount,
        instanceByteCount,
        alignof(InstanceGpuData)
    );
    outMaterialTypedBlob = graph.copyUploadData(
        bytes + instanceMaterialByteCount + instanceByteCount,
        materialTypedByteCount,
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

    if(m_preparedHybridHardwareFallbackRecorded){
        auto& state = m_rayTracingState;
        if(
            state.m_shadowInstanceMaterialBuffer.get() == m_preparedHybridHardwareFallbackInstanceMaterialBuffer.get()
            && state.m_shadowInstanceBuffer.get() == m_preparedHybridHardwareFallbackInstanceBuffer.get()
            && state.m_shadowMaterialTypedBuffer.get() == m_preparedHybridHardwareFallbackMaterialTypedBuffer.get()
            && state.m_shadowInstanceMaterialCapacity == m_preparedHybridHardwareFallbackInstanceMaterialCapacity
            && state.m_shadowInstanceCapacity == m_preparedHybridHardwareFallbackInstanceCapacity
            && state.m_shadowMaterialTypedCapacity == m_preparedHybridHardwareFallbackMaterialTypedCapacity
            && state.m_shadowInstanceMaterialHeapHandle == m_preparedHybridHardwareFallbackInstanceMaterialHeapHandle
            && state.m_shadowInstanceHeapHandle == m_preparedHybridHardwareFallbackInstanceHeapHandle
            && state.m_shadowMaterialTypedHeapHandle == m_preparedHybridHardwareFallbackMaterialTypedHeapHandle
        ){
            state.m_hwShadowMaterialContextHash = m_preparedHybridHardwareFallbackMaterialContextHash;
            state.m_hwShadowMaterialContextHashValid = m_preparedHybridHardwareFallbackStatic;
            state.m_swShadowMaterialContextHashValid = false;
        }
        else
            state.m_hwShadowMaterialContextHashValid = false;
    }
    clearPreparedHybridHardwareMaterialContextFallback();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

