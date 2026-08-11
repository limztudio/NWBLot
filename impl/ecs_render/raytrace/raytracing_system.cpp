// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_system.h"

#include <impl/ecs_render/raytrace/rt_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererRayTracingSystem::RendererRayTracingSystem(RendererSystem& renderer)
    : RendererSystemSubsystemBase<RendererSystem>(renderer)
    , m_preparedShadowTraceGeometryBuffers(arena())
    , m_acceptedShadowTraceGeometryBuffers(arena())
    , m_preparedCausticEmissionTargetBytes(arena())
    , m_preparedShadowInstanceMaterialBytes(arena())
    , m_preparedShadowInstanceBytes(arena())
    , m_preparedShadowMaterialTypedBytes(arena())
    , m_preparedSceneBvhNodeBytes(arena())
    , m_preparedSceneBvhInstanceBytes(arena())
    , m_preparedSceneTlasInstances(arena())
    , m_preparedSceneTlasBlases(arena())
{}

RendererRayTracingSystem::~RendererRayTracingSystem() = default;

void RendererRayTracingSystem::confirmShadowVisibilitySubmission(const Core::QueueSubmissionToken& submissionToken){
    if(
        !rayTracingState().m_swShadowEdgeStatsPending
        || !rayTracingState().m_swShadowEdgeStatsPendingSubmissionUnconfirmed
        || !submissionToken.valid()
        || !submissionToken.hasPhysicalQueueIdentity()
    )
        return;

    rayTracingState().m_swShadowEdgeStatsPendingSubmissionID = submissionToken.value;
    rayTracingState().m_swShadowEdgeStatsPendingSubmissionPhysicalQueue = Core::GpuPhysicalQueueId{
        submissionToken.physicalQueueIndex,
        submissionToken.deviceGeneration,
    };
    rayTracingState().m_swShadowEdgeStatsPendingSubmissionUnconfirmed = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::logCapabilityOnce(){
    if(rayTracingState().m_capabilityLogged)
        return;

    rayTracingState().m_capabilityLogged = true;
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: ray tracing capability - accel struct {}, pipeline {}, ray query {}")
        , graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        , graphics().queryFeatureSupport(Core::Feature::RayTracingPipeline)
        , graphics().queryFeatureSupport(Core::Feature::RayQuery)
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::shadowVisibilityResourcesPreflighted()const noexcept{
    return m_shadowVisibilityResourcesPreflighted;
}

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
    clearPreparedShadowMaterialContext();
    const auto& state = rayTracingState();
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
    const auto& state = rayTracingState();
    if(
        !m_preparedShadowMaterialContextReady
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

    const auto& state = rayTracingState();
    if(
        m_preparedShadowMaterialContextRoute == PreparedShadowMaterialContextRoute::None
        || m_preparedShadowInstanceMaterialBytes.empty()
        || m_preparedShadowInstanceBytes.empty()
        || m_preparedShadowMaterialTypedBytes.empty()
        || state.m_shadowInstanceMaterialBuffer.get() != m_preparedShadowInstanceMaterialBuffer.get()
        || state.m_shadowInstanceBuffer.get() != m_preparedShadowInstanceBuffer.get()
        || state.m_shadowMaterialTypedBuffer.get() != m_preparedShadowMaterialTypedBuffer.get()
        || state.m_shadowInstanceMaterialCapacity != m_preparedShadowInstanceMaterialCapacity
        || state.m_shadowInstanceCapacity != m_preparedShadowInstanceCapacity
        || state.m_shadowMaterialTypedCapacity != m_preparedShadowMaterialTypedCapacity
        || state.m_shadowInstanceMaterialHeapHandle != m_preparedShadowInstanceMaterialHeapHandle
        || state.m_shadowInstanceHeapHandle != m_preparedShadowInstanceHeapHandle
        || state.m_shadowMaterialTypedHeapHandle != m_preparedShadowMaterialTypedHeapHandle
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frozen shadow material-context payload no longer matches preflight storage"));
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
    if(!m_preparedShadowMaterialContextReady)
        return;

    if(m_preparedShadowMaterialContextRoute == PreparedShadowMaterialContextRoute::Hardware){
        rayTracingState().m_hwShadowMaterialContextHash = m_preparedShadowMaterialContextHash;
        rayTracingState().m_hwShadowMaterialContextHashValid = m_preparedShadowMaterialContextStatic;
        rayTracingState().m_swShadowMaterialContextHashValid = false;
    }
    else if(m_preparedShadowMaterialContextRoute == PreparedShadowMaterialContextRoute::Software){
        rayTracingState().m_swShadowMaterialContextHash = m_preparedShadowMaterialContextHash;
        rayTracingState().m_swShadowMaterialContextHashValid = m_preparedShadowMaterialContextStatic;
        rayTracingState().m_hwShadowMaterialContextHashValid = false;
    }
    clearPreparedShadowMaterialContext();
}

void RendererRayTracingSystem::clearPreparedSceneBvh()noexcept{
    m_preparedSceneBvhNodeBytes.clear();
    m_preparedSceneBvhInstanceBytes.clear();
    m_preparedSceneBvhNodeBuffer = nullptr;
    m_preparedSceneBvhInstanceBuffer = nullptr;
    m_preparedSceneBvhNodeHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_preparedSceneBvhInstanceHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_preparedSceneBvhNodeCount = 0u;
    m_preparedSceneBvhInstanceCount = 0u;
    m_preparedSceneBvhNodeCapacity = 0u;
    m_preparedSceneBvhInstanceCapacity = 0u;
    m_preparedSceneBvhStaticSceneHash = 0u;
    m_preparedSceneBvhStatic = false;
    m_preparedSceneBvhReady = false;
}

bool RendererRayTracingSystem::capturePreparedSceneBvh(
    const bool staticScene,
    const u64 staticSceneHash,
    const void* const nodeData,
    const usize nodeCount,
    const usize nodeByteCount,
    const void* const instanceData,
    const usize instanceCount,
    const usize instanceByteCount
){
    clearPreparedSceneBvh();
    const auto& state = rayTracingState();
    const bool validStorage =
        state.m_sceneBvhNodeBuffer
        && state.m_sceneInstanceBuffer
        && state.m_sceneBvhNodeCapacity >= nodeCount
        && state.m_sceneInstanceCapacity >= instanceCount
        && state.m_sceneBvhNodeHeapHandle.valid()
        && state.m_sceneBvhNodeHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
        && state.m_sceneInstanceHeapHandle.valid()
        && state.m_sceneInstanceHeapHandle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer
    ;
    if(
        !nodeData
        || !instanceData
        || nodeCount == 0u
        || instanceCount == 0u
        || nodeByteCount != nodeCount * sizeof(NwbBvhNodeGpu)
        || instanceByteCount != instanceCount * sizeof(SceneSwBvhInstanceGpu)
        || !validStorage
    )
        return false;

    m_preparedSceneBvhNodeBytes.resize(nodeByteCount);
    m_preparedSceneBvhInstanceBytes.resize(instanceByteCount);
    NWB_MEMCPY(
        m_preparedSceneBvhNodeBytes.data(),
        m_preparedSceneBvhNodeBytes.size(),
        nodeData,
        nodeByteCount
    );
    NWB_MEMCPY(
        m_preparedSceneBvhInstanceBytes.data(),
        m_preparedSceneBvhInstanceBytes.size(),
        instanceData,
        instanceByteCount
    );
    m_preparedSceneBvhNodeBuffer = state.m_sceneBvhNodeBuffer;
    m_preparedSceneBvhInstanceBuffer = state.m_sceneInstanceBuffer;
    m_preparedSceneBvhNodeHeapHandle = state.m_sceneBvhNodeHeapHandle;
    m_preparedSceneBvhInstanceHeapHandle = state.m_sceneInstanceHeapHandle;
    m_preparedSceneBvhNodeCount = nodeCount;
    m_preparedSceneBvhInstanceCount = instanceCount;
    m_preparedSceneBvhNodeCapacity = state.m_sceneBvhNodeCapacity;
    m_preparedSceneBvhInstanceCapacity = state.m_sceneInstanceCapacity;
    m_preparedSceneBvhStaticSceneHash = staticSceneHash;
    m_preparedSceneBvhStatic = staticScene;
    m_preparedSceneBvhReady = true;
    return true;
}

bool RendererRayTracingSystem::matchesPreparedSceneBvh(
    const bool staticScene,
    const u64 staticSceneHash,
    const void* const nodeData,
    const usize nodeCount,
    const usize nodeByteCount,
    const void* const instanceData,
    const usize instanceCount,
    const usize instanceByteCount
)const{
    const auto& state = rayTracingState();
    if(
        !m_preparedSceneBvhReady
        || m_preparedSceneBvhStatic != staticScene
        || m_preparedSceneBvhStaticSceneHash != staticSceneHash
        || !nodeData
        || !instanceData
        || m_preparedSceneBvhNodeCount != nodeCount
        || m_preparedSceneBvhInstanceCount != instanceCount
        || m_preparedSceneBvhNodeBytes.size() != nodeByteCount
        || m_preparedSceneBvhInstanceBytes.size() != instanceByteCount
        || nodeByteCount != nodeCount * sizeof(NwbBvhNodeGpu)
        || instanceByteCount != instanceCount * sizeof(SceneSwBvhInstanceGpu)
        || state.m_sceneBvhNodeBuffer.get() != m_preparedSceneBvhNodeBuffer.get()
        || state.m_sceneInstanceBuffer.get() != m_preparedSceneBvhInstanceBuffer.get()
        || state.m_sceneBvhNodeCapacity != m_preparedSceneBvhNodeCapacity
        || state.m_sceneInstanceCapacity != m_preparedSceneBvhInstanceCapacity
        || state.m_sceneBvhNodeHeapHandle != m_preparedSceneBvhNodeHeapHandle
        || state.m_sceneInstanceHeapHandle != m_preparedSceneBvhInstanceHeapHandle
        || !state.m_sceneBvhNodeHeapHandle.valid()
        || state.m_sceneBvhNodeHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
        || !state.m_sceneInstanceHeapHandle.valid()
        || state.m_sceneInstanceHeapHandle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
    )
        return false;
    return
        NWB_MEMCMP(m_preparedSceneBvhNodeBytes.data(), nodeData, nodeByteCount) == 0
        && NWB_MEMCMP(m_preparedSceneBvhInstanceBytes.data(), instanceData, instanceByteCount) == 0
    ;
}

bool RendererRayTracingSystem::retainPreparedSceneBvhUploads(
    Core::GpuTaskGraph& graph,
    Core::GpuUploadBlobId& outNodeBlob,
    Core::GpuUploadBlobId& outInstanceBlob
)const{
    outNodeBlob = {};
    outInstanceBlob = {};
    if(!m_preparedSceneBvhReady)
        return true;

    const auto& state = rayTracingState();
    if(
        m_preparedSceneBvhNodeBytes.empty()
        || m_preparedSceneBvhInstanceBytes.empty()
        || m_preparedSceneBvhNodeBytes.size() != m_preparedSceneBvhNodeCount * sizeof(NwbBvhNodeGpu)
        || m_preparedSceneBvhInstanceBytes.size() != m_preparedSceneBvhInstanceCount * sizeof(SceneSwBvhInstanceGpu)
        || state.m_sceneBvhNodeBuffer.get() != m_preparedSceneBvhNodeBuffer.get()
        || state.m_sceneInstanceBuffer.get() != m_preparedSceneBvhInstanceBuffer.get()
        || state.m_sceneBvhNodeCapacity != m_preparedSceneBvhNodeCapacity
        || state.m_sceneInstanceCapacity != m_preparedSceneBvhInstanceCapacity
        || state.m_sceneBvhNodeHeapHandle != m_preparedSceneBvhNodeHeapHandle
        || state.m_sceneInstanceHeapHandle != m_preparedSceneBvhInstanceHeapHandle
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: frozen software scene-BVH payload no longer matches preflight storage"));
        return false;
    }

    outNodeBlob = graph.copyUploadData(
        m_preparedSceneBvhNodeBytes.data(),
        m_preparedSceneBvhNodeBytes.size(),
        alignof(NwbBvhNodeGpu)
    );
    outInstanceBlob = graph.copyUploadData(
        m_preparedSceneBvhInstanceBytes.data(),
        m_preparedSceneBvhInstanceBytes.size(),
        alignof(SceneSwBvhInstanceGpu)
    );
    return outNodeBlob.valid() && outInstanceBlob.valid();
}

void RendererRayTracingSystem::confirmPreparedSceneBvhUploads()noexcept{
    if(!m_preparedSceneBvhReady)
        return;

    if(m_preparedSceneBvhStatic){
        rayTracingState().m_sceneSwBvhStaticSceneHash = m_preparedSceneBvhStaticSceneHash;
        rayTracingState().m_sceneSwBvhStaticSceneHashValid = true;
    }
    else
        rayTracingState().m_sceneSwBvhStaticSceneHashValid = false;
    clearPreparedSceneBvh();
}

void RendererRayTracingSystem::clearPreparedSceneTlasBuild()noexcept{
    m_preparedSceneTlasInstances.clear();
    m_preparedSceneTlasBlases.clear();
    m_preparedSceneTlas = nullptr;
    m_preparedSceneTlasBackingBuffer = nullptr;
    m_preparedSceneTlasHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_preparedSceneTlasMaxInstances = 0u;
    m_preparedSceneTlasStaticSceneHash = 0u;
    m_preparedSceneTlasStatic = false;
    m_preparedSceneTlasReady = false;
}

bool RendererRayTracingSystem::capturePreparedSceneTlasBuild(
    const bool staticScene,
    const u64 staticSceneHash,
    const Vector<Core::RayTracingInstanceDesc, Core::Alloc::ScratchArena>& instances,
    const Vector<Core::RayTracingAccelStructHandle, Core::Alloc::ScratchArena>& instanceBlases
){
    clearPreparedSceneTlasBuild();
    const auto& state = rayTracingState();
    if(
        instances.empty()
        || instances.size() != instanceBlases.size()
        || !state.m_tlas
        || !state.m_tlas->getBackingBufferHandle()
        || state.m_tlasMaxInstances < instances.size()
        || !state.m_tlasHeapHandle.valid()
        || state.m_tlasHeapHandle.descriptorClass() != Core::GpuDescriptorClass::AccelStruct
    )
        return false;

    m_preparedSceneTlasInstances.reserve(instances.size());
    m_preparedSceneTlasBlases.reserve(instanceBlases.size());
    for(usize index = 0u; index < instances.size(); ++index){
        const Core::RayTracingAccelStructHandle& blas = instanceBlases[index];
        if(!blas || instances[index].bottomLevelAS != blas.get()){
            clearPreparedSceneTlasBuild();
            return false;
        }
        m_preparedSceneTlasInstances.push_back(instances[index]);
        m_preparedSceneTlasBlases.push_back(blas);
    }
    m_preparedSceneTlas = state.m_tlas;
    m_preparedSceneTlasBackingBuffer = state.m_tlas->getBackingBufferHandle();
    m_preparedSceneTlasHeapHandle = state.m_tlasHeapHandle;
    m_preparedSceneTlasMaxInstances = state.m_tlasMaxInstances;
    m_preparedSceneTlasStaticSceneHash = staticSceneHash;
    m_preparedSceneTlasStatic = staticScene;
    m_preparedSceneTlasReady = true;
    return true;
}

bool RendererRayTracingSystem::recordPreparedSceneTlasBuild(Core::CommandList& commandList){
    const auto& state = rayTracingState();
    if(
        !m_preparedSceneTlasReady
        || m_preparedSceneTlasInstances.empty()
        || m_preparedSceneTlasInstances.size() != m_preparedSceneTlasBlases.size()
        || state.m_tlas.get() != m_preparedSceneTlas.get()
        || !state.m_tlas
        || state.m_tlas->getBackingBufferHandle().get() != m_preparedSceneTlasBackingBuffer.get()
        || state.m_tlasMaxInstances != m_preparedSceneTlasMaxInstances
        || state.m_tlasHeapHandle != m_preparedSceneTlasHeapHandle
        || !state.m_tlasHeapHandle.valid()
        || state.m_tlasHeapHandle.descriptorClass() != Core::GpuDescriptorClass::AccelStruct
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen scene TLAS build no longer matches preflight storage"));
        return false;
    }
    for(usize index = 0u; index < m_preparedSceneTlasInstances.size(); ++index){
        if(
            !m_preparedSceneTlasBlases[index]
            || m_preparedSceneTlasInstances[index].bottomLevelAS != m_preparedSceneTlasBlases[index].get()
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen scene TLAS build lost a referenced BLAS"));
            return false;
        }
    }

    // Vulkan's TLAS recorder issues the build directly. Publish the actual write/read sequence explicitly so this
    // graph task's declared AccelStructRead final state remains a truthful cross-packet and cross-frame handoff.
    commandList.setAccelStructState(m_preparedSceneTlas.get(), Core::ResourceStates::AccelStructWrite);
    commandList.commitBarriers();
    commandList.buildTopLevelAccelStruct(
        m_preparedSceneTlas.get(),
        m_preparedSceneTlasInstances.data(),
        m_preparedSceneTlasInstances.size(),
        Core::RayTracingAccelStructBuildFlags::PreferFastTrace
    );
    commandList.setAccelStructState(m_preparedSceneTlas.get(), Core::ResourceStates::AccelStructRead);
    commandList.commitBarriers();
    rayTracingState().m_tlasDeviceAddress = m_preparedSceneTlas->getDeviceAddress();
    return true;
}

bool RendererRayTracingSystem::preparedSceneTlasBuildReady()const noexcept{
    return m_preparedSceneTlasReady;
}

void RendererRayTracingSystem::confirmPreparedSceneTlasBuild()noexcept{
    if(!m_preparedSceneTlasReady)
        return;

    auto& state = rayTracingState();
    if(
        state.m_tlas.get() == m_preparedSceneTlas.get()
        && state.m_tlas
        && state.m_tlas->getBackingBufferHandle().get() == m_preparedSceneTlasBackingBuffer.get()
        && state.m_tlasMaxInstances == m_preparedSceneTlasMaxInstances
        && state.m_tlasHeapHandle == m_preparedSceneTlasHeapHandle
        && m_preparedSceneTlasStatic
    ){
        state.m_tlasStaticSceneHash = m_preparedSceneTlasStaticSceneHash;
        state.m_tlasStaticSceneHashValid = true;
    }
    else
        state.m_tlasStaticSceneHashValid = false;
    clearPreparedSceneTlasBuild();
}

bool RendererRayTracingSystem::freezePreparedShadowTraceGeometryBuffers(){
    m_preparedShadowTraceGeometryBuffers.clear();
    if(!m_shadowVisibilityResourcesPreflighted)
        return false;

    // Keep accepted invisible meshes: their last Prefix tail is still their real state. Prune only resources whose
    // owning mesh has been removed, so the retained handles cannot pin retired mesh storage indefinitely.
    const auto meshStillOwnsBuffer = [&](const Core::Buffer* const buffer){
        for(auto meshIt = meshState().m_meshes.begin(); meshIt != meshState().m_meshes.end(); ++meshIt){
            const MeshResources& mesh = meshIt.value();
            if(
                mesh.positionBuffer.get() == buffer
                || mesh.triangleIndexBuffer.get() == buffer
                || mesh.attributeBuffer.get() == buffer
                || mesh.swBvhNodeBuffer.get() == buffer
            )
                return true;
        }
        return false;
    };
    for(auto acceptedIt = m_acceptedShadowTraceGeometryBuffers.begin(); acceptedIt != m_acceptedShadowTraceGeometryBuffers.end();){
        if(!meshStillOwnsBuffer(acceptedIt->get()))
            acceptedIt = m_acceptedShadowTraceGeometryBuffers.erase(acceptedIt);
        else
            ++acceptedIt;
    }

    // The raw tables are populated by preflight.  Select only the active frozen backend plan: a hardware-only frame
    // intentionally leaves its old software tables intact, and those stale pointers must never enter this graph.
    const bool includeHardware = m_shadowVisibilityHardwareSupported && m_shadowVisibilityTraceResourcesPreflighted;
    const bool includeSoftware = m_shadowVisibilityTraceResourcesPreflighted
        && (!m_shadowVisibilityHardwareSupported || m_shadowVisibilityHybridPipelinePreflighted)
    ;
    if(!includeHardware && !includeSoftware)
        return true;

    const auto wasNormalizedByAcceptedPacket = [&](const Core::Buffer* const buffer){
        for(const Core::BufferHandle& acceptedBuffer : m_acceptedShadowTraceGeometryBuffers){
            if(acceptedBuffer.get() == buffer)
                return true;
        }
        return false;
    };
    const auto appendBuffer = [&](
        const Core::BufferHandle& buffer,
        const MeshResources& mesh,
        const AStringView identitySuffix,
        const u8 role
    ){
        if(!buffer)
            return false;
        for(PreparedShadowTraceGeometryBuffer& existing : m_preparedShadowTraceGeometryBuffers){
            if(existing.buffer.get() == buffer.get()){
                existing.roles |= role;
                return true;
            }
        }

        const Name identity = DeriveName(mesh.meshName, identitySuffix);
        if(!identity)
            return false;
        m_preparedShadowTraceGeometryBuffers.push_back(PreparedShadowTraceGeometryBuffer{
            .buffer = buffer,
            .identity = identity,
            // Fresh/preflight-created buffers retain their creation state (normally Common).  Only a buffer that an
            // accepted preparation/prefix tail explicitly normalized may enter the next graph as ShaderResource.
            .initialState = wasNormalizedByAcceptedPacket(buffer.get())
                ? Core::ResourceStates::ShaderResource
                : buffer->getDescription().initialState,
            .roles = role,
        });
        return true;
    };
    const auto appendSelected = [&]<typename SelectedBuffersT>(
        const SelectedBuffersT& selectedBuffers,
        const Core::BufferHandle MeshResources::* const bufferMember,
        const AStringView identitySuffix,
        const u8 role
    ){
        for(Core::Buffer* const selectedBuffer : selectedBuffers){
            if(!selectedBuffer)
                return false;

            bool found = false;
            for(auto meshIt = meshState().m_meshes.begin(); meshIt != meshState().m_meshes.end(); ++meshIt){
                const MeshResources& mesh = meshIt.value();
                const Core::BufferHandle& buffer = mesh.*bufferMember;
                if(buffer.get() != selectedBuffer)
                    continue;
                if(!appendBuffer(buffer, mesh, identitySuffix, role))
                    return false;
                found = true;
                break;
            }
            if(!found)
                return false;
        }
        return true;
    };
    // Shadow Prepare rebuilds runtime and dirty meshes even when they have no scene instance this frame.  Keep those
    // build inputs in the frozen graph set too: otherwise an off-screen build can leave a buffer in BLAS-input/UAV
    // state, then a later frame would import that same physical buffer as Common when it becomes visible.
    const auto appendPendingBlasBuildInputs = [&]{
        for(auto meshIt = meshState().m_meshes.begin(); meshIt != meshState().m_meshes.end(); ++meshIt){
            const MeshResources& mesh = meshIt.value();
            if(!mesh.runtimeMesh && !mesh.blasBuildPending)
                continue;
            if(
                !appendBuffer(
                    mesh.positionBuffer,
                    mesh,
                    AStringView(":shadow_trace_hw_position"),
                    PreparedShadowTraceGeometryRole::HardwarePosition
                )
                || !appendBuffer(
                    mesh.triangleIndexBuffer,
                    mesh,
                    AStringView(":shadow_trace_hw_index"),
                    PreparedShadowTraceGeometryRole::HardwareIndex
                )
            )
                return false;
        }
        return true;
    };
    const auto appendPendingSwBvhBuildInputs = [&]{
        for(auto meshIt = meshState().m_meshes.begin(); meshIt != meshState().m_meshes.end(); ++meshIt){
            const MeshResources& mesh = meshIt.value();
            if(!mesh.runtimeMesh && !mesh.swBvhBuildPending)
                continue;
            if(
                !appendBuffer(
                    mesh.swBvhNodeBuffer,
                    mesh,
                    AStringView(":shadow_trace_sw_nodes"),
                    PreparedShadowTraceGeometryRole::SoftwareNode
                )
                || !appendBuffer(
                    mesh.positionBuffer,
                    mesh,
                    AStringView(":shadow_trace_sw_position"),
                    PreparedShadowTraceGeometryRole::SoftwarePosition
                )
                || !appendBuffer(
                    mesh.triangleIndexBuffer,
                    mesh,
                    AStringView(":shadow_trace_sw_index"),
                    PreparedShadowTraceGeometryRole::SoftwareIndex
                )
            )
                return false;
        }
        return true;
    };

    bool collected = true;
    if(includeHardware){
        collected =
            appendSelected(
                rayTracingState().m_shadowMeshPositionBuffers,
                &MeshResources::positionBuffer,
                AStringView(":shadow_trace_hw_position"),
                PreparedShadowTraceGeometryRole::HardwarePosition
            )
            && appendSelected(
                rayTracingState().m_shadowMeshIndexBuffers,
                &MeshResources::triangleIndexBuffer,
                AStringView(":shadow_trace_hw_index"),
                PreparedShadowTraceGeometryRole::HardwareIndex
            )
            && appendSelected(
                rayTracingState().m_shadowMeshAttributeBuffers,
                &MeshResources::attributeBuffer,
                AStringView(":shadow_trace_hw_attribute"),
                PreparedShadowTraceGeometryRole::HardwareAttribute
            )
            && appendPendingBlasBuildInputs()
        ;
    }
    if(collected && includeSoftware){
        collected =
            appendSelected(
                rayTracingState().m_swShadowMeshNodeBuffers,
                &MeshResources::swBvhNodeBuffer,
                AStringView(":shadow_trace_sw_nodes"),
                PreparedShadowTraceGeometryRole::SoftwareNode
            )
            && appendSelected(
                rayTracingState().m_swShadowMeshPositionBuffers,
                &MeshResources::positionBuffer,
                AStringView(":shadow_trace_sw_position"),
                PreparedShadowTraceGeometryRole::SoftwarePosition
            )
            && appendSelected(
                rayTracingState().m_swShadowMeshIndexBuffers,
                &MeshResources::triangleIndexBuffer,
                AStringView(":shadow_trace_sw_index"),
                PreparedShadowTraceGeometryRole::SoftwareIndex
            )
            && appendSelected(
                rayTracingState().m_swShadowMeshAttributeBuffers,
                &MeshResources::attributeBuffer,
                AStringView(":shadow_trace_sw_attribute"),
                PreparedShadowTraceGeometryRole::SoftwareAttribute
            )
            && appendPendingSwBvhBuildInputs()
        ;
    }
    if(!collected)
        m_preparedShadowTraceGeometryBuffers.clear();
    else
        m_acceptedShadowTraceGeometryBuffers.reserve(m_preparedShadowTraceGeometryBuffers.size());
    return collected;
}

const PreparedShadowTraceGeometryBufferVector& RendererRayTracingSystem::preparedShadowTraceGeometryBuffers()const noexcept{
    return m_preparedShadowTraceGeometryBuffers;
}

void RendererRayTracingSystem::normalizePreparedShadowTraceGeometryBuffers(Core::CommandList& commandList)const{
    for(const PreparedShadowTraceGeometryBuffer& resource : m_preparedShadowTraceGeometryBuffers){
        if(resource.buffer)
            commandList.setBufferState(resource.buffer.get(), Core::ResourceStates::ShaderResource);
    }
}

void RendererRayTracingSystem::confirmPreparedShadowTraceGeometryNormalization()noexcept{
    for(const PreparedShadowTraceGeometryBuffer& resource : m_preparedShadowTraceGeometryBuffers){
        bool known = false;
        for(const Core::BufferHandle& acceptedBuffer : m_acceptedShadowTraceGeometryBuffers){
            if(acceptedBuffer.get() == resource.buffer.get()){
                known = true;
                break;
            }
        }
        if(resource.buffer && !known)
            m_acceptedShadowTraceGeometryBuffers.push_back(resource.buffer);
    }
}

void RendererRayTracingSystem::invalidatePreparedShadowTraceGeometryBuffers()noexcept{
    m_preparedShadowTraceGeometryBuffers.clear();
    m_acceptedShadowTraceGeometryBuffers.clear();
}

void RendererRayTracingSystem::discardPreflightShadowVisibilityResources()noexcept{
    m_preparedShadowTraceGeometryBuffers.clear();
    m_preparedCausticEmissionTargetBytes.clear();
    clearPreparedShadowMaterialContext();
    clearPreparedSceneBvh();
    clearPreparedSceneTlasBuild();
    m_shadowVisibilityPreparedTargets = nullptr;
    m_shadowVisibilityResourcesPreflighted = false;
    m_shadowVisibilityHardwareSupported = false;
    m_shadowVisibilityTraceResourcesPreflighted = false;
    m_shadowVisibilityHybridResourcesPreflighted = false;
    m_shadowVisibilityBackendPipelinePreflighted = false;
    m_shadowVisibilityHybridPipelinePreflighted = false;

    // A rejected preparation packet may have selected newly grown storage but never uploaded its contents.  Keep
    // the allocations, invalidate every semantic cache, and force both static and runtime meshes through a safe
    // rebuild on the next preflight.
    rayTracingState().m_tlasStaticSceneHashValid = false;
    rayTracingState().m_sceneSwBvhStaticSceneHashValid = false;
    rayTracingState().m_hwShadowMaterialContextHashValid = false;
    rayTracingState().m_swShadowMaterialContextHashValid = false;
    rayTracingState().m_hybridTransparentShadowReady = false;
    rayTracingState().m_surfelEnabled = false;
    rayTracingState().m_surfelUseHwTrace = false;

    // Recording updates these flags optimistically.  If the shared packet never submits, make every retained
    // acceleration structure rebuildable instead of allowing a later frame to consume its unrecorded contents.
    auto& meshes = meshState().m_meshes;
    for(auto it = meshes.begin(); it != meshes.end(); ++it){
        MeshResources& meshResources = it.value();
        if(meshResources.blas)
            meshResources.blasBuildPending = true;
        if(meshResources.swBvhNodeBuffer || meshResources.swBvhParentBuffer){
            meshResources.swBvhBuildPending = true;
            meshResources.swBvhTopologyBuilt = false;
        }
        meshResources.blasRefitsSinceRebuild = 0u;
        meshResources.swBvhRefitsSinceRebuild = 0u;
    }
}

bool RendererRayTracingSystem::preflightShadowVisibilityResources(
    DeferredFrameTargets& targets,
    Core::Alloc::ScratchArena& scratchArena
){
    // A new frame replaces the previous preflight plan, but does not invalidate retained acceleration data.  Full
    // invalidation is reserved for a rejected packet or resource teardown, where recorded work may not submit.
    m_shadowVisibilityPreparedTargets = nullptr;
    m_shadowVisibilityResourcesPreflighted = false;
    m_shadowVisibilityHardwareSupported = false;
    m_shadowVisibilityTraceResourcesPreflighted = false;
    m_shadowVisibilityHybridResourcesPreflighted = false;
    m_shadowVisibilityBackendPipelinePreflighted = false;
    m_shadowVisibilityHybridPipelinePreflighted = false;
    clearPreparedShadowMaterialContext();
    clearPreparedSceneBvh();
    clearPreparedSceneTlasBuild();
    // Surfel GI is a per-frame consumer of the scene selected below. Never let an empty or rejected preflight reuse
    // the preceding frame's backend selection and dispatch against stale trace inputs.
    rayTracingState().m_hybridTransparentShadowReady = false;
    rayTracingState().m_surfelEnabled = false;
    rayTracingState().m_surfelUseHwTrace = false;
    if(!targets.shadowVisibility)
        return false;
    // The trace material-context selector is itself a global UniformBuffer heap entry. Establish it before gathering
    // so the deferred graph imports the final backing handle rather than a recording-time replacement.
    if(!ensureRayTraceMaterialContextSlotsBuffer())
        return false;

    // Caustic target storage is selected before graph compilation. Uploading the gathered payload remains in the
    // graph-owned preparation packet.
    if(!prepareCausticEmissionTargetResources(scratchArena))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: caustic emission-target gather failed"));

    m_shadowVisibilityHardwareSupported =
        graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && graphics().queryFeatureSupport(Core::Feature::RayQuery)
    ;
    if(m_shadowVisibilityHardwareSupported){
        if(!preparePendingMeshBlasResources()){
            if(!ensureRayTraceMaterialContextSlotsHeapHandle())
                return false;
            m_shadowVisibilityPreparedTargets = &targets;
            m_shadowVisibilityResourcesPreflighted = true;
            return true;
        }

        const bool sceneReady = prepareSceneTlasResources(scratchArena);
        if(!sceneReady){
            if(!ensureRayTraceMaterialContextSlotsHeapHandle())
                return false;
            m_shadowVisibilityPreparedTargets = &targets;
            m_shadowVisibilityResourcesPreflighted = true;
            return true;
        }
        m_shadowVisibilityTraceResourcesPreflighted = true;

        // Hardware path casts the OPAQUE (binary) shadow via inline RayQuery.
        const bool backendReady =
            ensureShadowPipeline()
        ;
        m_shadowVisibilityBackendPipelinePreflighted = backendReady;

        // Hybrid TRANSPARENT shadow: when the scene holds a transparent occluder, also build the software scene/mesh
        // BVH and traversal pipeline. Its gather matches buildSceneTlas's (same RendererComponent view, aligned
        // conditions), so the scene-BVH leaf index equals the hardware InstanceID and the material context it builds
        // remains byte-identical -- leaving the HW caustic (which reads that context by InstanceID) untouched. The render
        // runs the SW traversal as a second pass that MULTIPLIES its colored transparent transmittance onto the opaque
        // mask. Built BEFORE prepareHwCausticResources so its heap slots are final before the outer preparation uploads
        // the shared material-context cbuffer. Opaque-only scenes skip all of this and pay no software cost.
        rayTracingState().m_hybridTransparentShadowReady = false;
        if(backendReady && rayTracingState().m_sceneHasTransparentOccluder){
            const bool meshResourcesReady = preparePendingMeshSwBvhResources();
            if(!meshResourcesReady)
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hybrid transparent shadow software BVH resource preparation failed"));
            // Guard m_swShadowMeshCount > 0: if no per-mesh software BVH was available this frame the software pass
            // simply does not run (HW opaque-only), rather than aborting.
            const bool swReady =
                meshResourcesReady
                && prepareSceneSwBvhResources(scratchArena)
                && rayTracingState().m_swShadowMeshCount > 0u
                && rayTracingState().m_sceneBvhInstanceCount > 0u
            ;
            m_shadowVisibilityHybridResourcesPreflighted = swReady;
            m_shadowVisibilityHybridPipelinePreflighted = swReady && ensureSwShadowPipeline();
            if(m_shadowVisibilityHybridPipelinePreflighted)
                rayTracingState().m_hybridTransparentShadowReady = true;
            else
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hybrid transparent software shadow preparation failed; transparent shadows absent this frame"));
        }
        // Hybrid recording can retain a valid hardware result when its later software half cannot complete. Keep the
        // established native writers for that two-stage fallback instead of publishing a presumed final graph blob.
        if(m_shadowVisibilityHybridResourcesPreflighted)
            clearPreparedShadowMaterialContext();
        // Hybrid tracing retains the established native HW/SW fallback. A later software failure is intentionally
        // non-fatal, so do not let an opaque-only frozen TLAS build publish a partial hybrid frame.
        if(rayTracingState().m_sceneHasTransparentOccluder)
            clearPreparedSceneTlasBuild();
        // The first graph-owned scene-BVH migration intentionally excludes every hardware route. In a hybrid frame
        // the later software half is non-fatal after hardware preparation has succeeded, so retain its established
        // direct writers instead of publishing a frozen pair whose acceptance would imply that SW work completed.
        clearPreparedSceneBvh();

        // Route the HW opaque shadow through the same half-res soft denoise chain the SW path uses: half-res jittered trace
        // -> temporal reproject-merge -> a-trous resolve -> upsample. The HW opaque-soft trace writes shadowSoftHalfA, then
        // the shared soft block denoises it through the same m_softShadow* state as the SW branch. Non-fatal: a failure
        // leaves m_softShadowReady false and the HW render falls back to the full-res 1-spp trace.
        rayTracingState().m_softShadowReady =
            backendReady
            && ensureShadowSoftPipeline()
            && ensureShadowGeometryDownsamplePipeline()
            && ensureSoftShadowResolvePipeline()
        ;
        if(backendReady && !rayTracingState().m_softShadowReady)
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: HW soft opaque shadow resource preparation failed; HW shadows fall back to the full-res trace this frame"));

        // Soft opaque shadow TEMPORAL accumulation (shared reproject-merge): same as the SW branch. Non-fatal: a failure
        // leaves m_softShadowTemporalReady false and the soft path feeds the raw trace straight into the a-trous.
        rayTracingState().m_softShadowTemporalReady =
            rayTracingState().m_softShadowReady
            && ensureShadowReprojectMergePipeline()
        ;
        if(rayTracingState().m_softShadowReady && !rayTracingState().m_softShadowTemporalReady)
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: HW soft opaque shadow temporal resource preparation failed; no temporal accumulation this frame"));

        // Colored transparent shadow uses the same soft transparent trace+fold path on the HW and SW shadow branches:
        // trace against the transparent-only software scene BVH, then multiply the denoised result onto soft-opaque
        // visibility inside dispatchSoftShadowDenoiseAndTransparentFold. Gate this on the HW opaque soft path and the
        // transparent SW BVH resources; opaque-only scenes leave m_softTransparentReady false. Non-fatal: a sub-ensure
        // failure leaves the state false and system.cpp can run the hybrid multiply fallback.
        rayTracingState().m_softTransparentReady =
            rayTracingState().m_softShadowReady
            && rayTracingState().m_hybridTransparentShadowReady
            && ensureSoftTransparentResolvePipeline()
        ;
        if(rayTracingState().m_softShadowReady && rayTracingState().m_hybridTransparentShadowReady && !rayTracingState().m_softTransparentReady)
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: HW soft transparent shadow resource preparation failed; colored shadows fall back to the hybrid multiply this frame"));

        rayTracingState().m_softTransparentTemporalReady =
            rayTracingState().m_softTransparentReady
            && rayTracingState().m_softShadowTemporalReady
        ;
        if(rayTracingState().m_softTransparentReady && rayTracingState().m_softShadowTemporalReady && !rayTracingState().m_softTransparentTemporalReady)
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: HW soft transparent shadow temporal resource preparation failed; no colored temporal accumulation this frame"));

        // Build the hardware caustic producer resources alongside the shadow ones (same TLAS + per-mesh geometry +
        // material context). Non-fatal to shadows: a failure leaves the caustic buffer black (the additive no-op),
        // mirroring the SW-branch prepareGpuBvhCausticResources call below.
        if(!prepareHwCausticResources(targets))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hardware caustic producer resource preparation failed"));

        // Enable surfel GI on the HW path: the HW RayQuery trace twin reuses the TLAS + the HW-resident per-mesh
        // geometry + InstanceID-material record the shadow/caustic path already built -- this is the ONLY place surfels
        // run on real RT hardware. Gated on the HW shadow backend being ready (so the TLAS + material context are
        // resident) + a non-empty TLAS. m_tlasInstanceCount (NOT m_sceneBvhInstanceCount, which the SW-only scene BVH
        // sets) is the HW instance count. Non-fatal: a failure leaves GI off this frame (the lighting uses hemiAmbient).
        if(backendReady && rayTracingState().m_tlasInstanceCount > 0u && rayTracingState().m_shadowMeshCount > 0u){
            rayTracingState().m_surfelEnabled = true;
            rayTracingState().m_surfelUseHwTrace = true;
            if(!prepareSurfelResources(targets))
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: HW surfel GI resource preparation failed"));
        }

        if(!ensureRayTraceMaterialContextSlotsHeapHandle())
            return false;
        m_shadowVisibilityPreparedTargets = &targets;
        m_shadowVisibilityResourcesPreflighted = true;
        return true;
    }

    // No hardware ray tracing: build/refit the per-mesh software BVHs from the already skinned geometry, then
    // build the per-frame software scene/instance BVH over them before the render pass consumes it.
    const bool meshResourcesReady = preparePendingMeshSwBvhResources();
    if(!meshResourcesReady)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software shadow BVH resource preparation failed"));
    if(!meshResourcesReady || !prepareSceneSwBvhResources(scratchArena)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software shadow scene BVH build failed"));
        m_shadowVisibilityPreparedTargets = &targets;
        m_shadowVisibilityResourcesPreflighted = true;
        return true;
    }

    // Enable surfel GI on the SW path (the surfel trace reuses the SW scene BVH the SW shadow/caustic paths built) and
    // create its resources in the prepare phase right after the scene BVH is resident. renderSurfelGi can then spawn,
    // hash, and trace on the same frame surfels become active. The pool/hash/pipeline resources live on
    // RendererRayTracingState so a resize does not reset convergence.
    if(rayTracingState().m_sceneBvhInstanceCount > 0u && rayTracingState().m_swShadowMeshCount > 0u){
        rayTracingState().m_surfelEnabled = true;
        rayTracingState().m_surfelUseHwTrace = false;   // SW branch: the surfel trace walks the SW scene BVH
        if(!prepareSurfelResources(targets))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: surfel GI resource preparation failed"));
    }

    if(rayTracingState().m_sceneBvhInstanceCount == 0u){
        if(!ensureRayTraceMaterialContextSlotsHeapHandle())
            return false;
        m_shadowVisibilityPreparedTargets = &targets;
        m_shadowVisibilityResourcesPreflighted = true;
        return true;
    }
    m_shadowVisibilityTraceResourcesPreflighted = true;

    const bool backendReady = ensureSwShadowPipeline();
    m_shadowVisibilityBackendPipelinePreflighted = backendReady;

    // Soft opaque shadow (all light types): build the geometry-downsample + a-trous resolve pipelines and heap-slot payloads so
    // the render can denoise the half-res jittered opaque trace into the full-res visibility. Non-fatal to shadows -- a
    // failure leaves m_softShadowReady false and the slot lights keep their hard opaque mask.
    rayTracingState().m_softShadowReady =
        backendReady
        && ensureShadowGeometryDownsamplePipeline()
        && ensureSoftShadowResolvePipeline()
    ;
    if(backendReady && !rayTracingState().m_softShadowReady)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: soft opaque shadow resource preparation failed; shadows hard this frame"));

    // Soft opaque shadow TEMPORAL accumulation: build the reproject-merge pipeline + its two front/back heap-slot
    // payloads AND the two temporal SOFT_HALF resolve variants (the a-trous then reads the accumulated history instead of the
    // raw trace). Always on when the resources build; non-fatal: a failure leaves m_softShadowTemporalReady false and the
    // soft path feeds the raw trace straight into the a-trous (the Stage-1/2 spatial-only fallback).
    rayTracingState().m_softShadowTemporalReady =
        rayTracingState().m_softShadowReady
        && ensureShadowReprojectMergePipeline()
    ;
    if(rayTracingState().m_softShadowReady && !rayTracingState().m_softShadowTemporalReady)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: soft opaque shadow temporal resource preparation failed; no temporal accumulation this frame"));

    // Soft COLORED TRANSPARENT shadow: build the RGB a-trous resolve pipeline + the parallel transparent resolve
    // heap-slot payloads (over the transparent half-res buffers), gated on the opaque soft path being ready (it shares the
    // geometry cache + the resolve binding layout + the ping-pong scratch). Non-fatal: a failure leaves m_softTransparentReady
    // false so the soft transparent fold is skipped and the transparent coarse/adaptive fallback runs (no double-fold --
    // they are exclusive). The transparent TEMPORAL path additionally needs the (shared) merge pipeline + the
    // parallel transparent merge heap-slot payloads; a failure there leaves m_softTransparentTemporalReady false and the transparent
    // resolve reads the raw colored trace straight into the RGB a-trous (the spatial fallback).
    rayTracingState().m_softTransparentReady =
        rayTracingState().m_softShadowReady
        && ensureSoftTransparentResolvePipeline()
    ;
    if(rayTracingState().m_softShadowReady && !rayTracingState().m_softTransparentReady)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: soft transparent shadow resource preparation failed; colored shadows fall back to the hard-ish path this frame"));

    rayTracingState().m_softTransparentTemporalReady =
        rayTracingState().m_softTransparentReady
        && rayTracingState().m_softShadowTemporalReady
    ;
    if(rayTracingState().m_softTransparentReady && rayTracingState().m_softShadowTemporalReady && !rayTracingState().m_softTransparentTemporalReady)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: soft transparent shadow temporal resource preparation failed; no colored temporal accumulation this frame"));

    // Build the software caustic producer + resolve resources alongside the SW shadow resources (same SW scene BVH +
    // per-mesh geometry). Non-fatal to shadows: a failure leaves the caustic buffer black (the additive no-op).
    if(!prepareGpuBvhCausticResources(targets))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software caustic producer resource preparation failed"));

    if(!ensureRayTraceMaterialContextSlotsHeapHandle())
        return false;
    m_shadowVisibilityPreparedTargets = &targets;
    m_shadowVisibilityResourcesPreflighted = true;
    return true;
}


bool RendererRayTracingSystem::recordPreflightShadowVisibilityResources(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    bool& outBackendReady,
    const bool causticEmissionTargetsGraphOwned,
    const bool surfelFrameConstantsGraphOwned,
    const bool shadowMaterialContextBatchGraphOwned,
    const bool sceneBvhBatchGraphOwned,
    const bool sceneTlasBuildGraphOwned
){
    outBackendReady = false;
    if(!m_shadowVisibilityResourcesPreflighted || m_shadowVisibilityPreparedTargets != &targets)
        return false;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_PrepareArena);
    if(!causticEmissionTargetsGraphOwned && !recordPreparedCausticEmissionTargets(commandList))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: caustic emission-target upload failed"));

    // A non-fatal preflight miss intentionally leaves tracing unavailable for this frame. Do not retry capacity
    // growth while recording: the shared graph has already frozen its imported resource identities.
    if(!m_shadowVisibilityTraceResourcesPreflighted)
        return true;

    if(m_shadowVisibilityHardwareSupported){
        if(!buildPendingMeshBlas(commandList)){
            if(sceneTlasBuildGraphOwned)
                return false;
            return true;
        }
        const bool sceneTlasReady = sceneTlasBuildGraphOwned
            ? recordPreparedSceneTlasBuild(commandList)
            : buildSceneTlas(commandList, scratchArena, shadowMaterialContextBatchGraphOwned)
        ;
        if(!sceneTlasReady){
            if(shadowMaterialContextBatchGraphOwned || sceneTlasBuildGraphOwned)
                return false;
            rayTracingState().m_surfelEnabled = false;
            rayTracingState().m_surfelUseHwTrace = false;
            return true;
        }

        outBackendReady = m_shadowVisibilityBackendPipelinePreflighted;
        rayTracingState().m_hybridTransparentShadowReady = false;
        if(
            outBackendReady
            && m_shadowVisibilityHybridResourcesPreflighted
            && rayTracingState().m_sceneHasTransparentOccluder
        ){
            if(!buildPendingMeshSwBvh(commandList))
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hybrid transparent shadow per-mesh software BVH build failed"));
            const bool swReady =
                buildSceneSwBvh(commandList, scratchArena, shadowMaterialContextBatchGraphOwned, false)
                && rayTracingState().m_swShadowMeshCount > 0u
                && rayTracingState().m_sceneBvhInstanceCount > 0u
                && m_shadowVisibilityHybridPipelinePreflighted
            ;
            if(swReady)
                rayTracingState().m_hybridTransparentShadowReady = true;
            else
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hybrid transparent software shadow recording failed; transparent shadows absent this frame"));
        }
        if(
            rayTracingState().m_surfelEnabled
            && !surfelFrameConstantsGraphOwned
            && !recordPreparedSurfelFrameConstants(commandList, targets)
        )
            return false;
        return true;
    }

    if(!buildPendingMeshSwBvh(commandList))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software shadow BVH update failed"));
    if(!buildSceneSwBvh(
        commandList,
        scratchArena,
        shadowMaterialContextBatchGraphOwned,
        sceneBvhBatchGraphOwned
    )){
        if(shadowMaterialContextBatchGraphOwned || sceneBvhBatchGraphOwned)
            return false;
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software shadow scene BVH recording failed"));
        rayTracingState().m_surfelEnabled = false;
        rayTracingState().m_surfelUseHwTrace = false;
        return true;
    }
    if(rayTracingState().m_sceneBvhInstanceCount == 0u){
        rayTracingState().m_surfelEnabled = false;
        rayTracingState().m_surfelUseHwTrace = false;
        return true;
    }

    outBackendReady = m_shadowVisibilityBackendPipelinePreflighted;
    if(
        rayTracingState().m_surfelEnabled
        && !surfelFrameConstantsGraphOwned
        && !recordPreparedSurfelFrameConstants(commandList, targets)
    )
        return false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

