// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "prepared_scene_bvh_upload.h"

#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/rt_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool PreparedSceneBvhUpload::StorageReady(const RendererRayTracingState& state, const usize nodeCount, const usize instanceCount)noexcept{
    return
        state.m_sceneBvhNodeBuffer
        && state.m_sceneInstanceBuffer
        && state.m_sceneBvhNodeCapacity >= nodeCount
        && state.m_sceneInstanceCapacity >= instanceCount
        && state.m_sceneBvhNodeHeapHandle.valid()
        && state.m_sceneBvhNodeHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && state.m_sceneInstanceHeapHandle.valid()
        && state.m_sceneInstanceHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
    ;
}


bool PreparedSceneBvhUpload::capture(
    const RendererRayTracingState& state,
    const bool staticScene,
    const u64 staticSceneHash,
    const void* const nodeData,
    const usize nodeCount,
    const usize nodeByteCount,
    const void* const instanceData,
    const usize instanceCount,
    const usize instanceByteCount
){
    clear();
    const bool validStorage = StorageReady(state, nodeCount, instanceCount);
    if(!nodeData || !instanceData || nodeCount == 0u || instanceCount == 0u || nodeByteCount != nodeCount * sizeof(NwbBvhNodeGpu) || instanceByteCount != instanceCount * sizeof(SceneSwBvhInstanceGpu) || !validStorage)
        return false;

    m_nodeBytes.resize(nodeByteCount);
    m_instanceBytes.resize(instanceByteCount);
    NWB_MEMCPY(m_nodeBytes.data(), m_nodeBytes.size(), nodeData, nodeByteCount);
    NWB_MEMCPY(m_instanceBytes.data(), m_instanceBytes.size(), instanceData, instanceByteCount);
    m_nodeBuffer = state.m_sceneBvhNodeBuffer;
    m_instanceBuffer = state.m_sceneInstanceBuffer;
    m_nodeHeapHandle = state.m_sceneBvhNodeHeapHandle;
    m_instanceHeapHandle = state.m_sceneInstanceHeapHandle;
    m_nodeCount = nodeCount;
    m_instanceCount = instanceCount;
    m_nodeCapacity = state.m_sceneBvhNodeCapacity;
    m_instanceCapacity = state.m_sceneInstanceCapacity;
    m_staticSceneHash = staticSceneHash;
    m_static = staticScene;
    m_ready = true;
    m_uploadRequired = true;
    return true;
}

bool PreparedSceneBvhUpload::captureCacheReuse(const RendererRayTracingState& state, const u64 staticSceneHash, const u32 instanceCount)noexcept{
    clear();
    if(instanceCount == 0u)
        return false;

    const usize requiredNodeCount = static_cast<usize>(instanceCount) * 2u - 1u;
    const bool validStorage = StorageReady(state, requiredNodeCount, instanceCount);
    if(!state.m_sceneSwBvhStaticSceneHashValid || state.m_sceneSwBvhStaticSceneHash != staticSceneHash || state.m_sceneBvhInstanceCount != instanceCount || !validStorage)
        return false;

    m_nodeBuffer = state.m_sceneBvhNodeBuffer;
    m_instanceBuffer = state.m_sceneInstanceBuffer;
    m_nodeHeapHandle = state.m_sceneBvhNodeHeapHandle;
    m_instanceHeapHandle = state.m_sceneInstanceHeapHandle;
    m_nodeCount = requiredNodeCount;
    m_instanceCount = instanceCount;
    m_nodeCapacity = state.m_sceneBvhNodeCapacity;
    m_instanceCapacity = state.m_sceneInstanceCapacity;
    m_staticSceneHash = staticSceneHash;
    m_static = true;
    m_ready = true;
    m_uploadRequired = false;
    return true;
}

void PreparedSceneBvhUpload::clear()noexcept{
    m_nodeBytes.clear();
    m_instanceBytes.clear();
    m_nodeBuffer = nullptr;
    m_instanceBuffer = nullptr;
    m_nodeHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_instanceHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_nodeCount = 0u;
    m_instanceCount = 0u;
    m_nodeCapacity = 0u;
    m_instanceCapacity = 0u;
    m_staticSceneHash = 0u;
    m_static = false;
    m_ready = false;
    m_uploadRequired = false;
}

Expected<PreparedSceneBvhUploads> PreparedSceneBvhUpload::retainUploads(const RendererRayTracingState& state, Core::GpuTaskGraph& graph)const{
    PreparedSceneBvhUploads result;
    if(!m_ready)
        return result;

    if(
        state.m_sceneBvhNodeBuffer.get() != m_nodeBuffer.get()
        || state.m_sceneInstanceBuffer.get() != m_instanceBuffer.get()
        || state.m_sceneBvhNodeCapacity != m_nodeCapacity
        || state.m_sceneInstanceCapacity != m_instanceCapacity
        || state.m_sceneBvhNodeHeapHandle != m_nodeHeapHandle
        || state.m_sceneInstanceHeapHandle != m_instanceHeapHandle
        || !state.m_sceneBvhNodeHeapHandle.valid()
        || state.m_sceneBvhNodeHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
        || !state.m_sceneInstanceHeapHandle.valid()
        || state.m_sceneInstanceHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frozen software scene-BVH identity no longer matches preflight storage"));
        return MakeUnexpected(Failure{});
    }
    if(!m_uploadRequired){
        if(!m_static || !state.m_sceneSwBvhStaticSceneHashValid || state.m_sceneSwBvhStaticSceneHash != m_staticSceneHash || state.m_sceneBvhInstanceCount != m_instanceCount){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frozen software scene-BVH cache identity is no longer accepted"));
            return MakeUnexpected(Failure{});
        }
        return result;
    }
    if(m_nodeBytes.empty() || m_instanceBytes.empty() || m_nodeBytes.size() != m_nodeCount * sizeof(NwbBvhNodeGpu) || m_instanceBytes.size() != m_instanceCount * sizeof(SceneSwBvhInstanceGpu)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frozen software scene-BVH payload is incomplete"));
        return MakeUnexpected(Failure{});
    }

    const auto nodeResult = graph.copyUploadData(m_nodeBytes.data(), m_nodeBytes.size(), alignof(NwbBvhNodeGpu));
    const auto instanceResult = graph.copyUploadData(m_instanceBytes.data(), m_instanceBytes.size(), alignof(SceneSwBvhInstanceGpu));
    if(!(nodeResult && instanceResult))
        return MakeUnexpected(Failure{});
    result.node = *nodeResult;
    result.instance = *instanceResult;
    return result;
}

bool PreparedSceneBvhUpload::publishAcceptedUpload(RendererRayTracingState& state)const noexcept{
    if(!m_ready || !m_uploadRequired)
        return false;

    if(m_static){
        state.m_sceneSwBvhStaticSceneHash = m_staticSceneHash;
        state.m_sceneSwBvhStaticSceneHashValid = true;
    }
    else
        state.m_sceneSwBvhStaticSceneHashValid = false;
    return true;
}

bool PreparedSceneBvhUpload::storageMatches(const RendererRayTracingState& state)const noexcept{
    return
        state.m_sceneBvhNodeBuffer.get() == m_nodeBuffer.get()
        && state.m_sceneInstanceBuffer.get() == m_instanceBuffer.get()
        && state.m_sceneBvhNodeCapacity == m_nodeCapacity
        && state.m_sceneInstanceCapacity == m_instanceCapacity
        && state.m_sceneBvhNodeHeapHandle == m_nodeHeapHandle
        && state.m_sceneInstanceHeapHandle == m_instanceHeapHandle
    ;
}

bool PreparedSceneBvhUpload::snapshotAccepted(const RendererRayTracingState& state)const noexcept{
    return m_uploadRequired || (m_static && state.m_sceneSwBvhStaticSceneHashValid && state.m_sceneSwBvhStaticSceneHash == m_staticSceneHash && state.m_sceneBvhInstanceCount == m_instanceCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

