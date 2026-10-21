// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "prepared_shadow_material_context.h"

#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/rt_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool PreparedShadowMaterialContext::StorageReady(const RendererRayTracingState& state, const usize instanceMaterialCount, const usize instanceCount, const usize materialTypedByteCount)noexcept{
    return
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
}


bool PreparedShadowMaterialContext::capture(
    const RendererRayTracingState& state,
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
    const bool validStorage = StorageReady(state, instanceMaterialCount, instanceCount, materialTypedByteCount);
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

    clear();
    m_instanceMaterialBytes.resize(instanceMaterialByteCount);
    m_instanceBytes.resize(instanceByteCount);
    m_materialTypedBytes.resize(materialTypedByteCount);
    NWB_MEMCPY(m_instanceMaterialBytes.data(), m_instanceMaterialBytes.size(), instanceMaterialData, instanceMaterialByteCount);
    NWB_MEMCPY(m_instanceBytes.data(), m_instanceBytes.size(), instanceData, instanceByteCount);
    NWB_MEMCPY(m_materialTypedBytes.data(), m_materialTypedBytes.size(), materialTypedData, materialTypedByteCount);
    captureStorage(state, hash, instanceMaterialCount, instanceCount, materialTypedByteCount);
    m_route = route;
    m_static = staticScene;
    m_ready = true;
    m_uploadRequired = true;
    return true;
}

bool PreparedShadowMaterialContext::captureCacheReuse(const RendererRayTracingState& state, const u64 hash, const usize instanceMaterialCount, const usize instanceCount, const usize materialTypedByteCount)noexcept{
    clear();
    if(instanceMaterialCount == 0u || instanceCount == 0u || materialTypedByteCount == 0u)
        return false;

    const bool validStorage = StorageReady(state, instanceMaterialCount, instanceCount, materialTypedByteCount);
    if(!state.m_swShadowMaterialContextHashValid || state.m_swShadowMaterialContextHash != hash || !validStorage)
        return false;

    captureStorage(state, hash, instanceMaterialCount, instanceCount, materialTypedByteCount);
    m_route = PreparedShadowMaterialContextRoute::Software;
    m_static = true;
    m_ready = true;
    m_uploadRequired = false;
    return true;
}

void PreparedShadowMaterialContext::clear()noexcept{
    m_instanceMaterialBytes.clear();
    m_instanceBytes.clear();
    m_materialTypedBytes.clear();
    m_instanceMaterialBuffer = nullptr;
    m_instanceBuffer = nullptr;
    m_materialTypedBuffer = nullptr;
    m_instanceMaterialHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_instanceHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_materialTypedHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_instanceMaterialCount = 0u;
    m_instanceCount = 0u;
    m_materialTypedUploadBytes = 0u;
    m_instanceMaterialCapacity = 0u;
    m_instanceCapacity = 0u;
    m_materialTypedCapacity = 0u;
    m_hash = 0u;
    m_route = PreparedShadowMaterialContextRoute::None;
    m_static = false;
    m_ready = false;
    m_uploadRequired = false;
}

Expected<PreparedShadowMaterialContextUploads> PreparedShadowMaterialContext::retainUploads(const RendererRayTracingState& state, Core::GpuTaskGraph& graph)const{
    PreparedShadowMaterialContextUploads result;
    if(!m_ready)
        return result;

    if(
        m_route == PreparedShadowMaterialContextRoute::None
        || m_instanceMaterialCount == 0u
        || m_instanceCount == 0u
        || m_materialTypedUploadBytes == 0u
        || m_instanceMaterialCapacity < m_instanceMaterialCount
        || m_instanceCapacity < m_instanceCount
        || m_materialTypedCapacity < m_materialTypedUploadBytes
        || state.m_shadowInstanceMaterialBuffer.get() != m_instanceMaterialBuffer.get()
        || state.m_shadowInstanceBuffer.get() != m_instanceBuffer.get()
        || state.m_shadowMaterialTypedBuffer.get() != m_materialTypedBuffer.get()
        || state.m_shadowInstanceMaterialCapacity != m_instanceMaterialCapacity
        || state.m_shadowInstanceCapacity != m_instanceCapacity
        || state.m_shadowMaterialTypedCapacity != m_materialTypedCapacity
        || state.m_shadowInstanceMaterialHeapHandle != m_instanceMaterialHeapHandle
        || state.m_shadowInstanceHeapHandle != m_instanceHeapHandle
        || state.m_shadowMaterialTypedHeapHandle != m_materialTypedHeapHandle
        || !state.m_shadowInstanceMaterialHeapHandle.valid()
        || state.m_shadowInstanceMaterialHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
        || !state.m_shadowInstanceHeapHandle.valid()
        || state.m_shadowInstanceHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
        || !state.m_shadowMaterialTypedHeapHandle.valid()
        || state.m_shadowMaterialTypedHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frozen shadow material-context identity no longer matches preflight storage"));
        return MakeUnexpected(Failure{});
    }
    if(!m_uploadRequired){
        if(
            m_route != PreparedShadowMaterialContextRoute::Software
            || !m_static
            || !state.m_swShadowMaterialContextHashValid
            || state.m_swShadowMaterialContextHash != m_hash
            || state.m_sceneBvhInstanceCount != m_instanceMaterialCount
            || state.m_sceneBvhInstanceCount != m_instanceCount
            || !m_instanceMaterialBytes.empty()
            || !m_instanceBytes.empty()
            || !m_materialTypedBytes.empty()
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frozen software material-context cache identity is no longer accepted"));
            return MakeUnexpected(Failure{});
        }
        return result;
    }
    if(
        m_instanceMaterialBytes.empty()
        || m_instanceBytes.empty()
        || m_materialTypedBytes.empty()
        || m_instanceMaterialBytes.size() != m_instanceMaterialCount * sizeof(NwbRtInstanceMaterialGpu)
        || m_instanceBytes.size() != m_instanceCount * sizeof(InstanceGpuData)
        || m_materialTypedBytes.size() != m_materialTypedUploadBytes
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frozen shadow material-context payload is incomplete"));
        return MakeUnexpected(Failure{});
    }

    const auto instanceMaterialResult = graph.copyUploadData(m_instanceMaterialBytes.data(), m_instanceMaterialBytes.size(), alignof(NwbRtInstanceMaterialGpu));
    const auto instanceResult = graph.copyUploadData(m_instanceBytes.data(), m_instanceBytes.size(), alignof(InstanceGpuData));
    const auto materialTypedResult = graph.copyUploadData(m_materialTypedBytes.data(), m_materialTypedBytes.size(), alignof(u32));
    if(!(instanceMaterialResult && instanceResult && materialTypedResult))
        return MakeUnexpected(Failure{});
    result.instanceMaterial = *instanceMaterialResult;
    result.instance = *instanceResult;
    result.materialTyped = *materialTypedResult;
    return result;
}

bool PreparedShadowMaterialContext::publishAcceptedUpload(RendererRayTracingState& state)const noexcept{
    if(!m_ready || !m_uploadRequired)
        return false;

    if(m_route == PreparedShadowMaterialContextRoute::Hardware){
        state.m_hwShadowMaterialContextHash = m_hash;
        state.m_hwShadowMaterialContextHashValid = m_static;
        state.m_swShadowMaterialContextHashValid = false;
    }
    else if(m_route == PreparedShadowMaterialContextRoute::Software){
        state.m_swShadowMaterialContextHash = m_hash;
        state.m_swShadowMaterialContextHashValid = m_static;
        state.m_hwShadowMaterialContextHashValid = false;
    }
    return true;
}

bool PreparedShadowMaterialContext::storageMatches(const RendererRayTracingState& state)const noexcept{
    return
        state.m_shadowInstanceMaterialBuffer.get() == m_instanceMaterialBuffer.get()
        && state.m_shadowInstanceBuffer.get() == m_instanceBuffer.get()
        && state.m_shadowMaterialTypedBuffer.get() == m_materialTypedBuffer.get()
        && state.m_shadowInstanceMaterialHeapHandle == m_instanceMaterialHeapHandle
        && state.m_shadowInstanceHeapHandle == m_instanceHeapHandle
        && state.m_shadowMaterialTypedHeapHandle == m_materialTypedHeapHandle
    ;
}

bool PreparedShadowMaterialContext::softwareSnapshotCovers(const usize sceneInstanceCount)const noexcept{
    return m_ready && m_route == PreparedShadowMaterialContextRoute::Software && m_instanceMaterialCount == sceneInstanceCount && m_instanceCount == sceneInstanceCount;
}

bool PreparedShadowMaterialContext::softwareSnapshotAccepted(const RendererRayTracingState& state)const noexcept{
    return m_uploadRequired || (m_static && state.m_swShadowMaterialContextHashValid && state.m_swShadowMaterialContextHash == m_hash && state.m_sceneBvhInstanceCount == m_instanceCount);
}

void PreparedShadowMaterialContext::captureStorage(const RendererRayTracingState& state, const u64 hash, const usize instanceMaterialCount, const usize instanceCount, const usize materialTypedByteCount)noexcept{
    m_instanceMaterialBuffer = state.m_shadowInstanceMaterialBuffer;
    m_instanceBuffer = state.m_shadowInstanceBuffer;
    m_materialTypedBuffer = state.m_shadowMaterialTypedBuffer;
    m_instanceMaterialHeapHandle = state.m_shadowInstanceMaterialHeapHandle;
    m_instanceHeapHandle = state.m_shadowInstanceHeapHandle;
    m_materialTypedHeapHandle = state.m_shadowMaterialTypedHeapHandle;
    m_instanceMaterialCount = instanceMaterialCount;
    m_instanceCount = instanceCount;
    m_materialTypedUploadBytes = materialTypedByteCount;
    m_instanceMaterialCapacity = state.m_shadowInstanceMaterialCapacity;
    m_instanceCapacity = state.m_shadowInstanceCapacity;
    m_materialTypedCapacity = state.m_shadowMaterialTypedCapacity;
    m_hash = hash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

