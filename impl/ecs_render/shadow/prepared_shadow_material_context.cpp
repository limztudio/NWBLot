// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "prepared_shadow_material_context.h"

#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/rt_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 PreparedShadowMaterialContext::StorageReady(const RendererRayTracingState& state, const usize instanceMaterialCount, const usize instanceCount, const usize materialTypedByteCount)noexcept{
    return
        state.m_shadowInstanceMaterialBuffer
        && state.m_shadowInstanceBuffer
        && state.m_shadowMaterialTypedBuffer
        && state.m_shadowInstanceMaterialCapacity >= instanceMaterialCount
        && state.m_shadowInstanceCapacity >= instanceCount
        && state.m_shadowMaterialTypedCapacity >= materialTypedByteCount
        && RayTracingDetail::IsHeapHandle(state.m_shadowInstanceMaterialHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && RayTracingDetail::IsHeapHandle(state.m_shadowInstanceHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && RayTracingDetail::IsHeapHandle(state.m_shadowMaterialTypedHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
    ;
}


b8 PreparedShadowMaterialContext::capture(
    const RendererRayTracingState& state,
    const PreparedShadowMaterialContextRoute::Enum route,
    const b8 staticScene,
    const u64 hash,
    const Span<const NwbRtInstanceMaterialGpu> instanceMaterials,
    const Span<const InstanceGpuData> instances,
    const Span<const u8> materialTypedBytes
){
    const usize instanceMaterialCount = instanceMaterials.size();
    const usize instanceCount = instances.size();
    const usize materialTypedByteCount = materialTypedBytes.size();
    const b8 validStorage = StorageReady(state, instanceMaterialCount, instanceCount, materialTypedByteCount);
    if(
        route == PreparedShadowMaterialContextRoute::None
        || instanceMaterialCount == 0u
        || instanceCount == 0u
        || materialTypedByteCount == 0u
        || !validStorage
    )
        return false;

    clear();
    CaptureShadowMaterialContextBytes(
        instanceMaterials,
        instances,
        materialTypedBytes,
        m_instanceMaterialBytes,
        m_instanceBytes,
        m_materialTypedBytes
    );
    captureStorage(state, hash, instanceMaterialCount, instanceCount, materialTypedByteCount);
    m_route = route;
    m_static = staticScene;
    m_ready = true;
    m_uploadRequired = true;
    return true;
}

b8 PreparedShadowMaterialContext::captureCacheReuse(const RendererRayTracingState& state, const u64 hash, const usize instanceMaterialCount, const usize instanceCount, const usize materialTypedByteCount)noexcept{
    clear();
    if(instanceMaterialCount == 0u || instanceCount == 0u || materialTypedByteCount == 0u)
        return false;

    const b8 validStorage = StorageReady(state, instanceMaterialCount, instanceCount, materialTypedByteCount);
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

    if(!ValidatePreparedShadowMaterialContextRetainState(
        m_route,
        m_instanceMaterialCount,
        m_instanceCount,
        m_materialTypedUploadBytes,
        storageMatches(state),
        state.m_shadowInstanceMaterialCapacity,
        m_instanceMaterialCapacity,
        state.m_shadowInstanceCapacity,
        m_instanceCapacity,
        state.m_shadowMaterialTypedCapacity,
        m_materialTypedCapacity,
        StorageReady(state, m_instanceMaterialCount, m_instanceCount, m_materialTypedUploadBytes),
        m_uploadRequired,
        m_static,
        state.m_swShadowMaterialContextHashValid,
        state.m_swShadowMaterialContextHash,
        m_hash,
        state.m_sceneBvhInstanceCount,
        m_instanceMaterialBytes.size(),
        m_instanceBytes.size(),
        m_materialTypedBytes.size()
    ))
        return MakeUnexpected(Failure{});
    if(!m_uploadRequired)
        return result;

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

b8 PreparedShadowMaterialContext::publishAcceptedUpload(RendererRayTracingState& state)const noexcept{
    return PublishAcceptedShadowMaterialContextUpload(
        m_ready,
        m_uploadRequired,
        m_route,
        m_hash,
        m_static,
        state.m_hwShadowMaterialContextHash,
        state.m_hwShadowMaterialContextHashValid,
        state.m_swShadowMaterialContextHash,
        state.m_swShadowMaterialContextHashValid
    );
}

b8 PreparedShadowMaterialContext::storageMatches(const RendererRayTracingState& state)const noexcept{
    return
        state.m_shadowInstanceMaterialBuffer.get() == m_instanceMaterialBuffer.get()
        && state.m_shadowInstanceBuffer.get() == m_instanceBuffer.get()
        && state.m_shadowMaterialTypedBuffer.get() == m_materialTypedBuffer.get()
        && state.m_shadowInstanceMaterialHeapHandle == m_instanceMaterialHeapHandle
        && state.m_shadowInstanceHeapHandle == m_instanceHeapHandle
        && state.m_shadowMaterialTypedHeapHandle == m_materialTypedHeapHandle
    ;
}

b8 PreparedShadowMaterialContext::softwareSnapshotCovers(const usize sceneInstanceCount)const noexcept{
    return IsPreparedShadowMaterialSoftwareSnapshotCovers(m_ready, m_route, m_instanceMaterialCount, m_instanceCount, sceneInstanceCount);
}

b8 PreparedShadowMaterialContext::softwareSnapshotAccepted(const RendererRayTracingState& state)const noexcept{
    return IsPreparedShadowMaterialSoftwareSnapshotAccepted(
        m_uploadRequired,
        m_static,
        state.m_swShadowMaterialContextHashValid,
        state.m_swShadowMaterialContextHash,
        m_hash,
        state.m_sceneBvhInstanceCount,
        m_instanceCount
    );
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


b8 MergePreparedShadowMaterialSampledTextures(const Span<const Core::TextureHandle> sampledTextures, MaterialSampledTextureCollector<Core::Alloc::GlobalArena>& collector){
    for(const Core::TextureHandle& texture : sampledTextures){
        if(!texture || !texture->getCreationDescription().name)
            return false;
        collector.append(texture);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
