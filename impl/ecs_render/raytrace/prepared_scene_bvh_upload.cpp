// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "prepared_scene_bvh_upload.h"

#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/scene_bvh_upload_policy.adhd.h>
#include <impl/ecs_render/raytrace/rt_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 PreparedSceneBvhUpload::StorageReady(const RendererRayTracingState& state, const usize nodeCount, const usize instanceCount)noexcept{
    return
        state.m_sceneBvhNodeBuffer
        && state.m_sceneInstanceBuffer
        && state.m_sceneBvhNodeCapacity >= nodeCount
        && state.m_sceneInstanceCapacity >= instanceCount
        && RayTracingDetail::IsHeapHandle(state.m_sceneBvhNodeHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        && RayTracingDetail::IsHeapHandle(state.m_sceneInstanceHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
    ;
}


b8 PreparedSceneBvhUpload::capture(
    const RendererRayTracingState& state,
    const b8 staticScene,
    const u64 staticSceneHash,
    const Span<const NwbBvhNodeGpu> nodes,
    const Span<const SceneSwBvhInstanceGpu> instances
){
    clear();
    const usize nodeCount = nodes.size();
    const usize instanceCount = instances.size();
    const b8 validStorage = StorageReady(state, nodeCount, instanceCount);
    if(nodeCount == 0u || instanceCount == 0u || !validStorage)
        return false;

    SceneBvhUploadPolicy().captureBytes(nodes, instances, m_nodeBytes, m_instanceBytes);
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

b8 PreparedSceneBvhUpload::captureCacheReuse(const RendererRayTracingState& state, const u64 staticSceneHash, const u32 instanceCount)noexcept{
    clear();
    if(instanceCount == 0u)
        return false;

    const usize requiredNodeCount = static_cast<usize>(instanceCount) * 2u - 1u;
    const b8 validStorage = StorageReady(state, requiredNodeCount, instanceCount);
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

    if(!SceneBvhUploadPolicy().validateRetainState(
        storageMatches(state),
        StorageReady(state, m_nodeCount, m_instanceCount),
        m_uploadRequired,
        m_static,
        state.m_sceneSwBvhStaticSceneHashValid,
        state.m_sceneSwBvhStaticSceneHash,
        m_staticSceneHash,
        state.m_sceneBvhInstanceCount,
        m_instanceCount,
        m_nodeBytes.size(),
        m_nodeCount,
        m_instanceBytes.size()
    ))
        return MakeUnexpected(Failure{});
    if(!m_uploadRequired)
        return result;

    const auto nodeResult = graph.copyUploadData(m_nodeBytes.data(), m_nodeBytes.size(), alignof(NwbBvhNodeGpu));
    const auto instanceResult = graph.copyUploadData(m_instanceBytes.data(), m_instanceBytes.size(), alignof(SceneSwBvhInstanceGpu));
    if(!(nodeResult && instanceResult))
        return MakeUnexpected(Failure{});
    result.node = *nodeResult;
    result.instance = *instanceResult;
    return result;
}

b8 PreparedSceneBvhUpload::publishAcceptedUpload(RendererRayTracingState& state)const noexcept{
    return SceneBvhUploadPolicy().publishAcceptedUpload(
        m_ready,
        m_uploadRequired,
        m_static,
        m_staticSceneHash,
        state.m_sceneSwBvhStaticSceneHash,
        state.m_sceneSwBvhStaticSceneHashValid
    );
}

b8 PreparedSceneBvhUpload::storageMatches(const RendererRayTracingState& state)const noexcept{
    return
        state.m_sceneBvhNodeBuffer.get() == m_nodeBuffer.get()
        && state.m_sceneInstanceBuffer.get() == m_instanceBuffer.get()
        && state.m_sceneBvhNodeCapacity == m_nodeCapacity
        && state.m_sceneInstanceCapacity == m_instanceCapacity
        && state.m_sceneBvhNodeHeapHandle == m_nodeHeapHandle
        && state.m_sceneInstanceHeapHandle == m_instanceHeapHandle
    ;
}

b8 PreparedSceneBvhUpload::snapshotAccepted(const RendererRayTracingState& state)const noexcept{
    return SceneBvhUploadPolicy().snapshotAccepted(
        m_uploadRequired,
        m_static,
        state.m_sceneSwBvhStaticSceneHashValid,
        state.m_sceneSwBvhStaticSceneHash,
        m_staticSceneHash,
        state.m_sceneBvhInstanceCount,
        m_instanceCount
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

