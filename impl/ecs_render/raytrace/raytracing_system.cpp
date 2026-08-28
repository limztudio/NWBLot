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
    , m_preparedShadowTraceMaterialSampledTextures(arena())
    , m_preparedCausticEmissionTargetBytes(arena())
    , m_preparedShadowInstanceMaterialBytes(arena())
    , m_preparedShadowInstanceBytes(arena())
    , m_preparedShadowMaterialTypedBytes(arena())
    , m_preparedHybridHardwareFallbackBytes(arena())
    , m_preparedSceneBvhNodeBytes(arena())
    , m_preparedSceneBvhInstanceBytes(arena())
    , m_preparedSceneSwBvhMeshes(arena())
    , m_preparedSceneTlasInstances(arena())
    , m_preparedSceneTlasBlases(arena())
    , m_preparedMeshBlasBuilds(arena())
    , m_preparedMeshSwBvhBuilds(arena())
{}

RendererRayTracingSystem::~RendererRayTracingSystem() = default;

void RendererRayTracingSystem::retireCompletedAdaptiveShadowStatisticsReadback(){
    auto& state = rayTracingState();
    if(
        !state.m_swShadowEdgeStatsPending
        || (state.m_swShadowEdgeStatsTick - state.m_swShadowEdgeStatsPendingTick) < s_SwShadowEdgeStatsLogDelay
        || state.m_swShadowEdgeStatsPendingSubmissionID == 0u
        || !state.m_swShadowEdgeStatsPendingSubmissionPhysicalQueue.valid()
        || graphics().getDevice().queueGetCompletedInstance(
            state.m_swShadowEdgeStatsPendingSubmissionPhysicalQueue
        ) < state.m_swShadowEdgeStatsPendingSubmissionID
    )
        return;

    const u32* const stats = static_cast<const u32*>(
        graphics().getDevice().mapBuffer(state.m_swShadowEdgeStatsReadback.get(), Core::CpuAccessMode::Read)
    );
    if(stats){
        const u32 traced = stats[NWB_SW_SHADOW_EDGE_STATS_TRACED];
        const u32 total = stats[NWB_SW_SHADOW_EDGE_STATS_TOTAL];
        graphics().getDevice().unmapBuffer(state.m_swShadowEdgeStatsReadback.get());
        const f64 fraction = (total > 0u) ? (100.0 * static_cast<f64>(traced) / static_cast<f64>(total)) : 0.0;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: SW shadow adaptive edge fraction = {}% ({} traced / {} total rays, threshold {})")
            , fraction
            , static_cast<u64>(traced)
            , static_cast<u64>(total)
            , static_cast<f64>(state.m_swShadowEdgeThreshold)
        );
    }
    state.m_swShadowEdgeStatsPending = false;
    state.m_swShadowEdgeStatsPendingSubmissionID = 0u;
    state.m_swShadowEdgeStatsPendingSubmissionPhysicalQueue = {};
}

void RendererRayTracingSystem::confirmGraphOwnedAdaptiveShadowSubmission(
    const GraphOwnedAdaptiveShadowPlan& plan,
    const bool adaptiveRouteRecorded,
    const Core::QueueSubmissionToken& submissionToken
){
    if(
        !plan.enabled
        || !adaptiveRouteRecorded
        || !submissionToken.valid()
        || !submissionToken.hasPhysicalQueueIdentity()
    )
        return;

    // The graph-owned plan can schedule clear/copy primitives before a later renderer callback discovers that its
    // producer is unavailable. Advance the diagnostic timeline only when that callback actually took the adaptive
    // route and its shared packet accepted.
    rayTracingState().m_swShadowEdgeStatsTick = plan.statsTick + 1u;
    if(!plan.captureStatsSnapshot)
        return;

    rayTracingState().m_swShadowEdgeStatsPending = true;
    rayTracingState().m_swShadowEdgeStatsPendingTick = plan.statsTick;
    rayTracingState().m_swShadowEdgeStatsPendingSubmissionID = submissionToken.value;
    rayTracingState().m_swShadowEdgeStatsPendingSubmissionPhysicalQueue = Core::GpuPhysicalQueueId{
        submissionToken.physicalQueueIndex,
        submissionToken.deviceGeneration,
    };
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

bool RendererRayTracingSystem::shadowVisibilityHardwareSupported()const noexcept{
    return m_shadowVisibilityHardwareSupported;
}

bool RendererRayTracingSystem::shadowVisibilitySoftwareResourcesPreflighted()const noexcept{
    return m_shadowVisibilityTraceResourcesPreflighted
        && (!m_shadowVisibilityHardwareSupported || m_shadowVisibilityHybridResourcesPreflighted)
    ;
}

bool RendererRayTracingSystem::hybridShadowVisibilityResourcesPreflighted()const noexcept{
    return m_shadowVisibilityHardwareSupported
        && m_shadowVisibilityHybridResourcesPreflighted
    ;
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
    const auto& state = rayTracingState();
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
    m_preparedHybridHardwareFallbackRendererMutationVersion = world().componentMutationVersion<RendererComponent>();
    m_preparedHybridHardwareFallbackTransformMutationVersion = world().componentMutationVersion<NWB::Impl::Scene::TransformComponent>();
    m_preparedHybridHardwareFallbackMaterialMutationVersion = world().componentMutationVersion<MaterialInstanceComponent>();
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

    const auto& state = rayTracingState();
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
    if(m_preparedShadowMaterialContextReady){
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
    }
    clearPreparedShadowMaterialContext();

    if(m_preparedHybridHardwareFallbackRecorded){
        auto& state = rayTracingState();
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

void RendererRayTracingSystem::clearPreparedSceneBvh()noexcept{
    clearPreparedSceneSwBvhTraversal();
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

void RendererRayTracingSystem::clearPreparedSceneSwBvhTraversal()noexcept{
    m_preparedSceneSwBvhMeshes.clear();
    m_preparedSceneSwBvhInstanceCount = 0u;
    m_preparedSceneSwBvhRendererMutationVersion = 0u;
    m_preparedSceneSwBvhTransformMutationVersion = 0u;
    m_preparedSceneSwBvhMaterialMutationVersion = 0u;
    m_preparedSceneSwBvhReady = false;
}

bool RendererRayTracingSystem::capturePreparedSceneSwBvhTraversal(
    const PreparedSceneSwBvhMesh* const meshes,
    const usize meshCount,
    const u32 instanceCount
){
    clearPreparedSceneSwBvhTraversal();
    const auto& state = rayTracingState();
    const auto validStorageHandle = [](const Core::GpuDescriptorHandle handle){
        return handle.valid() && handle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer;
    };
    if(
        !meshes
        || meshCount == 0u
        || meshCount > static_cast<usize>(Limit<u32>::s_Max)
        || instanceCount == 0u
        || !m_preparedSceneBvhReady
        || !m_preparedShadowMaterialContextReady
        || m_preparedShadowMaterialContextRoute != PreparedShadowMaterialContextRoute::Software
        || m_preparedSceneBvhInstanceCount != instanceCount
        || m_preparedShadowInstanceMaterialCount != instanceCount
        || m_preparedShadowInstanceCount != instanceCount
        || state.m_sceneBvhNodeBuffer.get() != m_preparedSceneBvhNodeBuffer.get()
        || state.m_sceneInstanceBuffer.get() != m_preparedSceneBvhInstanceBuffer.get()
        || state.m_sceneBvhNodeCapacity != m_preparedSceneBvhNodeCapacity
        || state.m_sceneInstanceCapacity != m_preparedSceneBvhInstanceCapacity
        || state.m_sceneBvhNodeHeapHandle != m_preparedSceneBvhNodeHeapHandle
        || state.m_sceneInstanceHeapHandle != m_preparedSceneBvhInstanceHeapHandle
        || state.m_shadowInstanceMaterialBuffer.get() != m_preparedShadowInstanceMaterialBuffer.get()
        || state.m_shadowInstanceBuffer.get() != m_preparedShadowInstanceBuffer.get()
        || state.m_shadowMaterialTypedBuffer.get() != m_preparedShadowMaterialTypedBuffer.get()
        || state.m_shadowInstanceMaterialHeapHandle != m_preparedShadowInstanceMaterialHeapHandle
        || state.m_shadowInstanceHeapHandle != m_preparedShadowInstanceHeapHandle
        || state.m_shadowMaterialTypedHeapHandle != m_preparedShadowMaterialTypedHeapHandle
    )
        return false;

    // The frozen traversal table is restored while Shadow Preparation records.  Reserve every live descriptor
    // table here, during preflight, so that restoring the immutable entries cannot grow renderer storage after the
    // graph has compiled.
    auto& mutableState = rayTracingState();
    mutableState.m_swShadowMeshNodeBuffers.reserve(meshCount);
    mutableState.m_swShadowMeshPositionBuffers.reserve(meshCount);
    mutableState.m_swShadowMeshIndexBuffers.reserve(meshCount);
    mutableState.m_swShadowMeshAttributeBuffers.reserve(meshCount);
    mutableState.m_swShadowMeshNodeHandles.reserve(meshCount);
    mutableState.m_swShadowMeshPositionHandles.reserve(meshCount);
    mutableState.m_swShadowMeshIndexHandles.reserve(meshCount);
    mutableState.m_swShadowMeshAttributeHandles.reserve(meshCount);
    m_preparedSceneSwBvhMeshes.reserve(meshCount);
    for(usize index = 0u; index < meshCount; ++index){
        const PreparedSceneSwBvhMesh& mesh = meshes[index];
        if(
            mesh.meshName == NAME_NONE
            || !mesh.nodeBuffer
            || !mesh.positionBuffer
            || !mesh.triangleIndexBuffer
            || !mesh.attributeBuffer
            || !validStorageHandle(mesh.nodeHeapHandle)
            || !validStorageHandle(mesh.positionHeapHandle)
            || !validStorageHandle(mesh.triangleIndexHeapHandle)
            || !validStorageHandle(mesh.attributeHeapHandle)
            || mesh.primitiveCount == 0u
            || mesh.nodeByteSize == 0u
            || mesh.positionByteSize == 0u
            || mesh.triangleIndexByteSize == 0u
            || mesh.attributeByteSize == 0u
        ){
            clearPreparedSceneSwBvhTraversal();
            return false;
        }
        m_preparedSceneSwBvhMeshes.push_back(mesh);
    }
    m_preparedSceneSwBvhInstanceCount = instanceCount;
    m_preparedSceneSwBvhRendererMutationVersion = world().componentMutationVersion<RendererComponent>();
    m_preparedSceneSwBvhTransformMutationVersion = world().componentMutationVersion<NWB::Impl::Scene::TransformComponent>();
    m_preparedSceneSwBvhMaterialMutationVersion = world().componentMutationVersion<MaterialInstanceComponent>();
    m_preparedSceneSwBvhReady = true;
    return true;
}

bool RendererRayTracingSystem::recordPreparedSceneSwBvhTraversal(const bool restoreMutableTables){
    const auto validStorageHandle = [](const Core::GpuDescriptorHandle handle){
        return handle.valid() && handle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer;
    };
    const auto& state = rayTracingState();
    if(
        !m_preparedSceneSwBvhReady
        || m_preparedSceneSwBvhMeshes.empty()
        || m_preparedSceneSwBvhInstanceCount == 0u
        || !m_preparedSceneBvhReady
        || !m_preparedShadowMaterialContextReady
        || m_preparedShadowMaterialContextRoute != PreparedShadowMaterialContextRoute::Software
        || m_preparedSceneBvhInstanceCount != m_preparedSceneSwBvhInstanceCount
        || m_preparedShadowInstanceMaterialCount != m_preparedSceneSwBvhInstanceCount
        || m_preparedShadowInstanceCount != m_preparedSceneSwBvhInstanceCount
        || state.m_sceneBvhNodeBuffer.get() != m_preparedSceneBvhNodeBuffer.get()
        || state.m_sceneInstanceBuffer.get() != m_preparedSceneBvhInstanceBuffer.get()
        || state.m_sceneBvhNodeCapacity != m_preparedSceneBvhNodeCapacity
        || state.m_sceneInstanceCapacity != m_preparedSceneBvhInstanceCapacity
        || state.m_sceneBvhNodeHeapHandle != m_preparedSceneBvhNodeHeapHandle
        || state.m_sceneInstanceHeapHandle != m_preparedSceneBvhInstanceHeapHandle
        || state.m_shadowInstanceMaterialBuffer.get() != m_preparedShadowInstanceMaterialBuffer.get()
        || state.m_shadowInstanceBuffer.get() != m_preparedShadowInstanceBuffer.get()
        || state.m_shadowMaterialTypedBuffer.get() != m_preparedShadowMaterialTypedBuffer.get()
        || state.m_shadowInstanceMaterialHeapHandle != m_preparedShadowInstanceMaterialHeapHandle
        || state.m_shadowInstanceHeapHandle != m_preparedShadowInstanceHeapHandle
        || state.m_shadowMaterialTypedHeapHandle != m_preparedShadowMaterialTypedHeapHandle
        || !validStorageHandle(state.m_sceneBvhNodeHeapHandle)
        || !validStorageHandle(state.m_sceneInstanceHeapHandle)
        || !validStorageHandle(state.m_shadowInstanceMaterialHeapHandle)
        || !validStorageHandle(state.m_shadowInstanceHeapHandle)
        || !validStorageHandle(state.m_shadowMaterialTypedHeapHandle)
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software scene traversal no longer matches preflight storage"));
        return false;
    }
    if(
        world().componentMutationVersion<RendererComponent>() != m_preparedSceneSwBvhRendererMutationVersion
        || world().componentMutationVersion<NWB::Impl::Scene::TransformComponent>() != m_preparedSceneSwBvhTransformMutationVersion
        || world().componentMutationVersion<MaterialInstanceComponent>() != m_preparedSceneSwBvhMaterialMutationVersion
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software scene inputs changed after graph preflight"));
        return false;
    }

    const auto matchesMesh = [this, &validStorageHandle](
        const MeshResources& mesh,
        const PreparedSceneSwBvhMesh& prepared
    ){
        const auto attributeCache = rayTracingState().m_swMeshHeapHandleCache.find(prepared.attributeBuffer.get());
        const bool topologyReady = mesh.swBvhTopologyBuilt || preparedMeshSwBvhBuildProducesTopology(mesh);
        return
            mesh.meshName == prepared.meshName
            && mesh.runtimeMesh == prepared.runtimeMesh
            && mesh.runtimeMeshVersion == prepared.runtimeMeshVersion
            && mesh.swBvhNodeBuffer.get() == prepared.nodeBuffer.get()
            && mesh.positionBuffer.get() == prepared.positionBuffer.get()
            && mesh.triangleIndexBuffer.get() == prepared.triangleIndexBuffer.get()
            && mesh.attributeBuffer.get() == prepared.attributeBuffer.get()
            && mesh.swBvhNodeHeapHandle == prepared.nodeHeapHandle
            && mesh.swBvhPositionHeapHandle == prepared.positionHeapHandle
            && mesh.swBvhTriangleIndexHeapHandle == prepared.triangleIndexHeapHandle
            && attributeCache != rayTracingState().m_swMeshHeapHandleCache.end()
            && attributeCache.value().handle == prepared.attributeHeapHandle
            && topologyReady
            && mesh.meshletPrimitiveIndexCount == prepared.primitiveCount * s_RayTracingTriangleIndexCount
            && mesh.swBvhNodeBuffer->getCreationDescription().byteSize == prepared.nodeByteSize
            && mesh.positionBuffer->getCreationDescription().byteSize == prepared.positionByteSize
            && mesh.triangleIndexBuffer->getCreationDescription().byteSize == prepared.triangleIndexByteSize
            && mesh.attributeBuffer->getCreationDescription().byteSize == prepared.attributeByteSize
            && validStorageHandle(prepared.nodeHeapHandle)
            && validStorageHandle(prepared.positionHeapHandle)
            && validStorageHandle(prepared.triangleIndexHeapHandle)
            && validStorageHandle(prepared.attributeHeapHandle)
        ;
    };

    const auto& meshes = meshState().m_meshes;
    for(const PreparedSceneSwBvhMesh& prepared : m_preparedSceneSwBvhMeshes){
        const auto found = meshes.find(prepared.meshName);
        if(found == meshes.end() || !matchesMesh(found.value(), prepared)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software scene lost mesh '{}'")
                , StringConvert(prepared.meshName.c_str())
            );
            return false;
        }
    }

    auto& mutableState = rayTracingState();
    if(!restoreMutableTables){
        // Pure-SW preflight already populated these exact live descriptor tables before graph declaration froze the
        // resource set.  Revalidate instead of clearing/rebuilding them while the accepting packet records: that
        // keeps the frozen path free of callback-time allocation and CPU scene-table publication.
        const bool tablesMatch =
            mutableState.m_swShadowMeshCount == static_cast<u32>(m_preparedSceneSwBvhMeshes.size())
            && mutableState.m_sceneBvhInstanceCount == m_preparedSceneSwBvhInstanceCount
            && mutableState.m_swShadowMeshNodeBuffers.size() == m_preparedSceneSwBvhMeshes.size()
            && mutableState.m_swShadowMeshPositionBuffers.size() == m_preparedSceneSwBvhMeshes.size()
            && mutableState.m_swShadowMeshIndexBuffers.size() == m_preparedSceneSwBvhMeshes.size()
            && mutableState.m_swShadowMeshAttributeBuffers.size() == m_preparedSceneSwBvhMeshes.size()
            && mutableState.m_swShadowMeshNodeHandles.size() == m_preparedSceneSwBvhMeshes.size()
            && mutableState.m_swShadowMeshPositionHandles.size() == m_preparedSceneSwBvhMeshes.size()
            && mutableState.m_swShadowMeshIndexHandles.size() == m_preparedSceneSwBvhMeshes.size()
            && mutableState.m_swShadowMeshAttributeHandles.size() == m_preparedSceneSwBvhMeshes.size()
        ;
        if(!tablesMatch){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen pure software scene traversal table changed after graph preflight"));
            return false;
        }
        for(usize index = 0u; index < m_preparedSceneSwBvhMeshes.size(); ++index){
            const PreparedSceneSwBvhMesh& prepared = m_preparedSceneSwBvhMeshes[index];
            if(
                mutableState.m_swShadowMeshNodeBuffers[index] != prepared.nodeBuffer.get()
                || mutableState.m_swShadowMeshPositionBuffers[index] != prepared.positionBuffer.get()
                || mutableState.m_swShadowMeshIndexBuffers[index] != prepared.triangleIndexBuffer.get()
                || mutableState.m_swShadowMeshAttributeBuffers[index] != prepared.attributeBuffer.get()
                || mutableState.m_swShadowMeshNodeHandles[index] != prepared.nodeHeapHandle
                || mutableState.m_swShadowMeshPositionHandles[index] != prepared.positionHeapHandle
                || mutableState.m_swShadowMeshIndexHandles[index] != prepared.triangleIndexHeapHandle
                || mutableState.m_swShadowMeshAttributeHandles[index] != prepared.attributeHeapHandle
            ){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen pure software scene traversal table no longer matches preflight"));
                return false;
            }
        }
        return true;
    }

    mutableState.m_swShadowMeshNodeBuffers.clear();
    mutableState.m_swShadowMeshPositionBuffers.clear();
    mutableState.m_swShadowMeshIndexBuffers.clear();
    mutableState.m_swShadowMeshAttributeBuffers.clear();
    mutableState.m_swShadowMeshNodeHandles.clear();
    mutableState.m_swShadowMeshPositionHandles.clear();
    mutableState.m_swShadowMeshIndexHandles.clear();
    mutableState.m_swShadowMeshAttributeHandles.clear();
    // capturePreparedSceneSwBvhTraversal reserved each table before graph compilation.  Do not allocate while
    // recording: every entry below is a retained immutable descriptor/buffer identity.
    for(const PreparedSceneSwBvhMesh& prepared : m_preparedSceneSwBvhMeshes){
        const auto attributeCache = mutableState.m_swMeshHeapHandleCache.find(prepared.attributeBuffer.get());
        if(attributeCache != mutableState.m_swMeshHeapHandleCache.end())
            attributeCache.value().seenThisFrame = true;
        mutableState.m_swShadowMeshNodeBuffers.push_back(prepared.nodeBuffer.get());
        mutableState.m_swShadowMeshPositionBuffers.push_back(prepared.positionBuffer.get());
        mutableState.m_swShadowMeshIndexBuffers.push_back(prepared.triangleIndexBuffer.get());
        mutableState.m_swShadowMeshAttributeBuffers.push_back(prepared.attributeBuffer.get());
        mutableState.m_swShadowMeshNodeHandles.push_back(prepared.nodeHeapHandle);
        mutableState.m_swShadowMeshPositionHandles.push_back(prepared.positionHeapHandle);
        mutableState.m_swShadowMeshIndexHandles.push_back(prepared.triangleIndexHeapHandle);
        mutableState.m_swShadowMeshAttributeHandles.push_back(prepared.attributeHeapHandle);
    }
    mutableState.m_swShadowMeshCount = static_cast<u32>(m_preparedSceneSwBvhMeshes.size());
    mutableState.m_sceneBvhInstanceCount = m_preparedSceneSwBvhInstanceCount;
    return true;
}

#if !defined(NWB_FINAL)
void RendererRayTracingSystem::forceHybridSceneTraversalFallbackForTesting()noexcept{
    m_forceHybridSceneTraversalFallbackForTesting = true;
}

void RendererRayTracingSystem::forceHybridSceneTraversalFallbackEveryFrameForTesting()noexcept{
    m_forceHybridSceneTraversalFallbackEveryFrameForTesting = true;
    m_expectHybridSceneTraversalRecoveryForTesting = false;
    m_reportedHybridSceneTraversalFallbackLoopForTesting = false;
    m_reportedHybridSceneTraversalFallbackLoopFailureForTesting = false;
    m_reportedHybridHardwareFallbackRestoreLoopForTesting = false;
}

#endif

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

bool RendererRayTracingSystem::recordPreparedSceneTlasBuild(
    Core::CommandList& commandList,
    const bool sceneTlasBuildStatesGraphOwned
){
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

    // Normal graph preparation declares Write before this callback and the adjacent state-only finalizer publishes
    // Read afterwards. Direct and compatibility callers retain the historical native Write -> Read bridge.
    if(!sceneTlasBuildStatesGraphOwned){
        commandList.setAccelStructState(m_preparedSceneTlas.get(), Core::ResourceStates::AccelStructWrite);
        commandList.commitBarriers();
    }
    commandList.buildTopLevelAccelStruct(
        m_preparedSceneTlas.get(),
        m_preparedSceneTlasInstances.data(),
        m_preparedSceneTlasInstances.size(),
        Core::RayTracingAccelStructBuildFlags::PreferFastTrace
    );
    if(!sceneTlasBuildStatesGraphOwned){
        commandList.setAccelStructState(m_preparedSceneTlas.get(), Core::ResourceStates::AccelStructRead);
        commandList.commitBarriers();
    }
    rayTracingState().m_tlasDeviceAddress = m_preparedSceneTlas->getDeviceAddress();
    return true;
}

bool RendererRayTracingSystem::preparedSceneTlasBuildReady()const noexcept{
    return m_preparedSceneTlasReady;
}

Core::ResourceStates::Mask RendererRayTracingSystem::sceneTlasBackingInitialState()const noexcept{
    const auto& state = rayTracingState();
    return state.m_tlasBackingFresh
        ? Core::ResourceStates::Common
        : Core::ResourceStates::Unknown
    ;
}

void RendererRayTracingSystem::confirmPreparedSceneTlasBuild()noexcept{
    if(!m_preparedSceneTlasReady)
        return;

    auto& state = rayTracingState();
    const bool preparedTlasMatchesCurrent =
        state.m_tlas.get() == m_preparedSceneTlas.get()
        && state.m_tlas
        && state.m_tlas->getBackingBufferHandle().get() == m_preparedSceneTlasBackingBuffer.get()
        && state.m_tlasMaxInstances == m_preparedSceneTlasMaxInstances
        && state.m_tlasHeapHandle == m_preparedSceneTlasHeapHandle
    ;
    if(
        preparedTlasMatchesCurrent
        && m_preparedSceneTlasStatic
    ){
        state.m_tlasStaticSceneHash = m_preparedSceneTlasStaticSceneHash;
        state.m_tlasStaticSceneHashValid = true;
    }
    else
        state.m_tlasStaticSceneHashValid = false;
    // This callback follows the accepted persistent-state handoff. A later graph import must use that binding
    // rather than reasserting Common for this backing generation.
    if(preparedTlasMatchesCurrent){
        state.m_tlasBackingFresh = false;
        state.m_tlasBackingStateHandoffPending = false;
    }
    clearPreparedSceneTlasBuild();
}

void RendererRayTracingSystem::confirmAcceptedShadowPrepareAccelStructStateHandoffs()noexcept{
    auto& state = rayTracingState();
    if(state.m_tlasBackingFresh && state.m_tlasBackingStateHandoffPending){
        state.m_tlasBackingFresh = false;
        state.m_tlasBackingStateHandoffPending = false;
    }

    auto& meshes = meshState().m_meshes;
    for(auto it = meshes.begin(); it != meshes.end(); ++it){
        MeshResources& meshResources = it.value();
        if(meshResources.blasBackingFresh && meshResources.blasBackingStateHandoffPending){
            meshResources.blasBackingFresh = false;
            meshResources.blasBackingStateHandoffPending = false;
        }
    }
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
        // Hybrid may still record its direct SW build before a later traversal-pipeline miss makes the transparent
        // fallback unavailable. Retain/import that native producer state by resource readiness, not pipeline use.
        && (!m_shadowVisibilityHardwareSupported || m_shadowVisibilityHybridResourcesPreflighted)
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
                : buffer->getCreationDescription().initialState,
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

const Vector<Core::BufferHandle, Core::Alloc::GlobalArena>&
RendererRayTracingSystem::acceptedShadowTraceGeometryBuffers()const noexcept{
    return m_acceptedShadowTraceGeometryBuffers;
}

const PreparedShadowTraceMaterialSampledTextureVector&
RendererRayTracingSystem::preparedShadowTraceMaterialSampledTextures()const noexcept{
    return m_preparedShadowTraceMaterialSampledTextures;
}

bool RendererRayTracingSystem::appendPreparedShadowTraceMaterialSampledTextures(
    const MaterialSurfaceInfo& materialInfo,
    Core::Alloc::ScratchArena& scratchArena
){
    Vector<Core::TextureHandle, Core::Alloc::ScratchArena> sampledTextures{ scratchArena };
    if(!m_renderer.materialSystem().appendPreparedMaterialSurfaceSampledTextures(materialInfo, sampledTextures))
        return false;

    for(const Core::TextureHandle& texture : sampledTextures){
        if(!texture || !texture->getCreationDescription().name)
            return false;

        bool alreadyCollected = false;
        for(const Core::TextureHandle& existing : m_preparedShadowTraceMaterialSampledTextures){
            if(existing.get() == texture.get()){
                alreadyCollected = true;
                break;
            }
        }
        if(!alreadyCollected)
            m_preparedShadowTraceMaterialSampledTextures.push_back(texture);
    }
    return true;
}

void RendererRayTracingSystem::clearPreparedShadowTraceMaterialSampledTextures()noexcept{
    m_preparedShadowTraceMaterialSampledTextures.clear();
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
    clearPreparedShadowTraceMaterialSampledTextures();
}

void RendererRayTracingSystem::discardPreflightShadowVisibilityResources()noexcept{
    m_preparedShadowTraceGeometryBuffers.clear();
    clearPreparedShadowTraceMaterialSampledTextures();
    m_preparedCausticEmissionTargetBytes.clear();
    clearPreparedShadowMaterialContext();
    clearPreparedHybridHardwareMaterialContextFallback();
    clearPreparedSceneBvh();
    clearPreparedSceneTlasBuild();
    clearPreparedMeshBlasBuilds();
    clearPreparedMeshSwBvhBuilds();
    rayTracingState().m_tlasBackingStateHandoffPending = false;
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
        meshResources.blasBackingStateHandoffPending = false;
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
    clearPreparedShadowTraceMaterialSampledTextures();
    clearPreparedShadowMaterialContext();
    clearPreparedHybridHardwareMaterialContextFallback();
    clearPreparedSceneBvh();
    clearPreparedSceneTlasBuild();
    clearPreparedMeshBlasBuilds();
    clearPreparedMeshSwBvhBuilds();
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
        // Per-mesh hardware BLAS work is independent from the later software material/scene gather.  Freeze it
        // for both opaque and hybrid frames. An opaque capture miss must drop the paired frozen TLAS; a hybrid miss
        // retains the established direct hardware path, whose result remains valid when the optional SW tail fails.
        if(!capturePreparedMeshBlasBuilds()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not freeze hardware BLAS build plan"));
            if(!rayTracingState().m_sceneHasTransparentOccluder)
                clearPreparedSceneTlasBuild();
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
            const bool swPipelineReady = meshResourcesReady && ensureSwShadowPipeline();
            const bool swReady =
                swPipelineReady
                && prepareSceneSwBvhResources(scratchArena)
                && rayTracingState().m_swShadowMeshCount > 0u
                && rayTracingState().m_sceneBvhInstanceCount > 0u
            ;
            m_shadowVisibilityHybridResourcesPreflighted = swReady;
            m_shadowVisibilityHybridPipelinePreflighted = swReady;
            if(m_shadowVisibilityHybridPipelinePreflighted){
                rayTracingState().m_hybridTransparentShadowReady = true;
                // The per-mesh build is independent from the later CPU scene/material gather. Freeze it as a
                // graph-owned plan when possible; a capture miss retains the native mesh-build compatibility path.
                if(!capturePreparedMeshSwBvhBuilds())
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not freeze hybrid transparent software BVH build plan"));
            }
            else
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hybrid transparent software shadow preparation failed; transparent shadows absent this frame"));
        }
        // A healthy hybrid packet finishes with the software-compatible material context: its node slots drive the
        // transparent traversal while its shared attribute slots remain valid for the HW caustic and surfel consumers.
        // Keep that exact immutable snapshot only after the software pipeline is available. An earlier failure leaves
        // the already-frozen HW material/TLAS plan intact, so opaque shadows remain graph-owned for this frame.
        // The scene-BVH pair is retained only for a healthy hybrid frame with its matching traversal table.
        if(!m_shadowVisibilityHybridResourcesPreflighted)
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
    // The software-only route can freeze every selected per-mesh build/refit because it has no later non-fatal
    // hardware fallback. A capture miss keeps the established direct recorder for this frame rather than mixing a
    // partially frozen operation with a live one.
    if(!capturePreparedMeshSwBvhBuilds())
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not freeze software BVH mesh build plan"));
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
    const bool shadowMaterialContextBatchGraphOwned,
    const bool sceneBvhBatchGraphOwned,
    const bool sceneTlasBuildGraphOwned,
    const bool meshBlasBuildsGraphOwned,
    const bool meshBlasGeometryBuildInputStatesGraphOwned,
    const bool meshSwBvhBuildsGraphOwned,
    const bool preparedMeshSwBvhBuildsRecordedByGraph,
    const bool deferHybridSoftwareTail
){
    outBackendReady = false;
    if(!m_shadowVisibilityResourcesPreflighted || m_shadowVisibilityPreparedTargets != &targets)
        return false;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_PrepareArena);

    // A non-fatal preflight miss intentionally leaves tracing unavailable for this frame. Do not retry capacity
    // growth while recording: the shared graph has already frozen its imported resource identities.
    if(!m_shadowVisibilityTraceResourcesPreflighted)
        return true;

    if(m_shadowVisibilityHardwareSupported){
        // When a complete hybrid preflight froze the final software-compatible context, the hardware TLAS must not
        // overwrite it before the optional software tail restores its retained traversal table. A tail miss restores
        // the current hardware context below; a precursor miss simply disables material consumers while preserving
        // the packet fallback.
        const bool hybridSoftwareMaterialContextGraphOwned =
            shadowMaterialContextBatchGraphOwned
            && m_shadowVisibilityHybridPipelinePreflighted
            && m_preparedShadowMaterialContextReady
            && m_preparedShadowMaterialContextRoute == PreparedShadowMaterialContextRoute::Software
        ;
        const auto discardHybridGraphMaterialContext = [&](){
            clearPreparedShadowMaterialContext();
            rayTracingState().m_hwShadowMaterialContextHashValid = false;
            rayTracingState().m_swShadowMaterialContextHashValid = false;
            rayTracingState().m_hybridTransparentShadowReady = false;
            rayTracingState().m_softTransparentReady = false;
            rayTracingState().m_softTransparentTemporalReady = false;
        };
        const auto disableHybridMaterialConsumers = [&](){
            rayTracingState().m_causticRefractiveInstanceCount = 0u;
            rayTracingState().m_surfelEnabled = false;
            rayTracingState().m_surfelUseHwTrace = false;
        };
        // A transparent scene keeps a valid opaque-HW fallback even when its later software tail cannot record.
        // If its independent frozen BLAS plan no longer matches, discard that plan and retry the established live
        // loop rather than rejecting the whole packet. Opaque-only frozen plans remain all-or-nothing.
        const bool hybridHardwareFallback = rayTracingState().m_sceneHasTransparentOccluder;
        bool meshBlasReady = meshBlasBuildsGraphOwned
            ? recordPreparedMeshBlasBuilds(
                commandList,
                true,
                meshBlasGeometryBuildInputStatesGraphOwned
            )
            : buildPendingMeshBlas(commandList)
        ;
        if(!meshBlasReady && meshBlasBuildsGraphOwned && hybridHardwareFallback){
            clearPreparedMeshBlasBuilds();
            meshBlasReady = buildPendingMeshBlas(commandList);
        }
        if(!meshBlasReady){
            if(
                sceneTlasBuildGraphOwned
                || (meshBlasBuildsGraphOwned && !hybridHardwareFallback)
            )
                return false;
            // A hybrid SW-BVH plan has not recorded when the HW precursor misses. Do not let packet acceptance
            // publish optimistic mesh topology state for work that never reached this command list.
            if(meshSwBvhBuildsGraphOwned)
                clearPreparedMeshSwBvhBuilds();
            if(sceneBvhBatchGraphOwned)
                clearPreparedSceneBvh();
            if(hybridSoftwareMaterialContextGraphOwned){
                discardHybridGraphMaterialContext();
                disableHybridMaterialConsumers();
            }
            return true;
        }
        // A healthy hybrid frozen TLAS plan is independent from the immutable software material/context uploads.
        // If its native record no longer matches, discard only the plan and retry the current direct TLAS build;
        // buildSceneTlas preserves a graph-owned SW material triple for the following optional-tail validation.
        const bool hybridSceneTlasFallback =
            sceneTlasBuildGraphOwned
            && m_shadowVisibilityHybridPipelinePreflighted
        ;
        bool sceneTlasReady = sceneTlasBuildGraphOwned
            ? recordPreparedSceneTlasBuild(commandList, true)
            : buildSceneTlas(
                commandList,
                scratchArena,
                shadowMaterialContextBatchGraphOwned
            )
        ;
        if(!sceneTlasReady && hybridSceneTlasFallback){
            clearPreparedSceneTlasBuild();
            sceneTlasReady = buildSceneTlas(
                commandList,
                scratchArena,
                shadowMaterialContextBatchGraphOwned
            );
        }
        if(!sceneTlasReady){
            if(
                (sceneTlasBuildGraphOwned && !hybridSceneTlasFallback)
                || (shadowMaterialContextBatchGraphOwned && !hybridSoftwareMaterialContextGraphOwned)
            )
                return false;
            rayTracingState().m_surfelEnabled = false;
            rayTracingState().m_surfelUseHwTrace = false;
            // As above, retain only plans whose native commands were actually recorded.
            if(meshSwBvhBuildsGraphOwned)
                clearPreparedMeshSwBvhBuilds();
            if(sceneBvhBatchGraphOwned)
                clearPreparedSceneBvh();
            if(hybridSoftwareMaterialContextGraphOwned){
                discardHybridGraphMaterialContext();
                disableHybridMaterialConsumers();
            }
            return true;
        }

        outBackendReady = m_shadowVisibilityBackendPipelinePreflighted;
        if(deferHybridSoftwareTail)
            return true;
        return recordPreflightHybridSoftwareTail(
            commandList,
            targets,
            outBackendReady,
            shadowMaterialContextBatchGraphOwned,
            sceneBvhBatchGraphOwned,
            meshSwBvhBuildsGraphOwned
        );
    }

    const bool meshSwBvhReady = meshSwBvhBuildsGraphOwned
        // Pure software Shadow Preparation has no preceding hardware BLAS producer. Its frozen position/index
        // inputs are declared as ShaderResource by the graph. When per-operation typed clears and compute tasks
        // already recorded in this same accepting packet, retain only the existing scene-BVH/material tail here.
        ? (preparedMeshSwBvhBuildsRecordedByGraph
            || recordPreparedMeshSwBvhBuilds(commandList, meshSwBvhBuildsGraphOwned))
        : buildPendingMeshSwBvh(commandList)
    ;
    if(!meshSwBvhReady){
        if(meshSwBvhBuildsGraphOwned)
            return false;
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software shadow BVH update failed"));
    }
    // Pure software frames have no opaque-HW fallback.  When both immutable upload batches and their matching
    // traversal table survived preflight, restore that exact table instead of regathering scene/material data while
    // this accepting graph packet records.  A stale frozen plan rejects the packet; the next preflight rebuilds it.
    const bool frozenPureSceneTraversal =
        shadowMaterialContextBatchGraphOwned
        && sceneBvhBatchGraphOwned
        && m_preparedSceneSwBvhReady
    ;
    const bool sceneSwBvhReady = frozenPureSceneTraversal
        ? recordPreparedSceneSwBvhTraversal(false)
        : buildSceneSwBvh(
            commandList,
            scratchArena,
            shadowMaterialContextBatchGraphOwned,
            sceneBvhBatchGraphOwned,
            meshSwBvhBuildsGraphOwned
        )
    ;
    if(!sceneSwBvhReady){
        if(frozenPureSceneTraversal)
            return false;
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
    return true;
}


bool RendererRayTracingSystem::recordPreflightHybridSoftwareTail(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool hardwareBackendReady,
    const bool shadowMaterialContextBatchGraphOwned,
    const bool sceneBvhBatchGraphOwned,
    const bool meshSwBvhBuildsGraphOwned,
    const bool meshSwBvhInputStatesGraphOwned,
    const void* const hybridHardwareFallbackInstanceMaterialData,
    const usize hybridHardwareFallbackInstanceMaterialByteCount,
    const void* const hybridHardwareFallbackInstanceData,
    const usize hybridHardwareFallbackInstanceByteCount,
    const void* const hybridHardwareFallbackMaterialTypedData,
    const usize hybridHardwareFallbackMaterialTypedByteCount
){
    if(!m_shadowVisibilityResourcesPreflighted || m_shadowVisibilityPreparedTargets != &targets)
        return false;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_PrepareArena);
    // When a complete hybrid preflight froze the final software-compatible context, a tail miss must restore its
    // retained hardware descriptor context before the first accepting packet closes. Keep that transaction here so
    // the graph callback can move independently without weakening the opaque-HW fallback.
    const bool hybridSoftwareMaterialContextGraphOwned =
        shadowMaterialContextBatchGraphOwned
        && m_shadowVisibilityHybridPipelinePreflighted
        && m_preparedShadowMaterialContextReady
        && m_preparedShadowMaterialContextRoute == PreparedShadowMaterialContextRoute::Software
    ;
    const auto discardHybridGraphMaterialContext = [&](){
        clearPreparedShadowMaterialContext();
        rayTracingState().m_hwShadowMaterialContextHashValid = false;
        rayTracingState().m_swShadowMaterialContextHashValid = false;
        rayTracingState().m_hybridTransparentShadowReady = false;
        rayTracingState().m_softTransparentReady = false;
        rayTracingState().m_softTransparentTemporalReady = false;
    };
    rayTracingState().m_hybridTransparentShadowReady = false;
    bool hybridMeshSwBvhBuildRecorded = false;
    bool hybridSceneBvhBuildRecorded = false;
    if(
        hardwareBackendReady
        && m_shadowVisibilityHybridResourcesPreflighted
        // Revalidate a frozen hybrid payload even when an intervening material edit removed its transparency.
        // Otherwise the graph upload could survive without either the SW consumer or a direct HW restoration.
        && (rayTracingState().m_sceneHasTransparentOccluder || hybridSoftwareMaterialContextGraphOwned)
    ){
        const bool meshSwBvhReady = meshSwBvhBuildsGraphOwned
            // A fully frozen and import-verified hybrid packet declares every prepared SW input as ShaderResource
            // on this tail. Its preceding Shadow Preparation callback supplied build inputs for the frozen BLAS
            // plan, so the packet runtime lowers their exact AccelStructBuildInput -> ShaderResource handoff before
            // this recorder runs. Direct, retry, and incomplete-plan paths retain the native bridge.
            ? recordPreparedMeshSwBvhBuilds(commandList, meshSwBvhInputStatesGraphOwned)
            : buildPendingMeshSwBvh(commandList)
        ;
        hybridMeshSwBvhBuildRecorded = meshSwBvhBuildsGraphOwned && meshSwBvhReady;
        if(!meshSwBvhReady){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hybrid transparent shadow per-mesh software BVH build failed"));
            // This failure is deliberately non-fatal: HW opaque shadows still submit. Drop the frozen plan so the
            // accepted packet cannot commit a topology build that did not record.
            if(meshSwBvhBuildsGraphOwned)
                clearPreparedMeshSwBvhBuilds();
        }
        // The direct compatibility loop historically continues gathering the scene after an unrelated mesh build
        // miss. Preserve that best-effort behavior; only a failed frozen mesh plan is all-or-nothing because its
        // scene gather may otherwise observe topology that never recorded.
        const bool canRecordSceneSwBvh = !meshSwBvhBuildsGraphOwned || meshSwBvhReady;
        // A complete healthy hybrid retains both graph-upload batches plus the exact distinct-mesh descriptor
        // table. Record it directly instead of rebuilding CPU BVH/material data. A source-version or resource miss
        // drops to the established direct revalidation path before deciding the optional-tail fallback.
        const bool hybridSceneTraversalGraphOwned =
            canRecordSceneSwBvh
            && shadowMaterialContextBatchGraphOwned
            && sceneBvhBatchGraphOwned
            && m_shadowVisibilityHybridPipelinePreflighted
        ;
        bool forceHybridSceneTraversalFallback = false;
#if !defined(NWB_FINAL)
        forceHybridSceneTraversalFallback =
            hybridSceneTraversalGraphOwned
            && (
                m_forceHybridSceneTraversalFallbackForTesting
                || m_forceHybridSceneTraversalFallbackEveryFrameForTesting
            )
        ;
        if(forceHybridSceneTraversalFallback){
            const bool oneShotFallback = m_forceHybridSceneTraversalFallbackForTesting;
            m_forceHybridSceneTraversalFallbackForTesting = false;
            if(oneShotFallback)
                m_expectHybridSceneTraversalRecoveryForTesting = true;
            if(oneShotFallback || !m_reportedHybridSceneTraversalFallbackLoopForTesting){
                m_reportedHybridSceneTraversalFallbackLoopForTesting = true;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: test forced hybrid software traversal fallback"));
            }
        }
#endif
        bool sceneSwBvhReady = !forceHybridSceneTraversalFallback && canRecordSceneSwBvh && (
            hybridSceneTraversalGraphOwned
                ? recordPreparedSceneSwBvhTraversal()
                : buildSceneSwBvh(
                    commandList,
                    scratchArena,
                    shadowMaterialContextBatchGraphOwned,
                    sceneBvhBatchGraphOwned,
                    meshSwBvhBuildsGraphOwned
                )
        );
        if(
            !sceneSwBvhReady
            && hybridSceneTraversalGraphOwned
            && !forceHybridSceneTraversalFallback
        ){
            sceneSwBvhReady = buildSceneSwBvh(
                commandList,
                scratchArena,
                shadowMaterialContextBatchGraphOwned,
                sceneBvhBatchGraphOwned,
                meshSwBvhBuildsGraphOwned
            );
        }
        hybridSceneBvhBuildRecorded = sceneBvhBatchGraphOwned && sceneSwBvhReady;
        const bool swReady =
            sceneSwBvhReady
            && rayTracingState().m_swShadowMeshCount > 0u
            && rayTracingState().m_sceneBvhInstanceCount > 0u
            && m_shadowVisibilityHybridPipelinePreflighted
        ;
        if(swReady){
            rayTracingState().m_hybridTransparentShadowReady = true;
#if !defined(NWB_FINAL)
            if(m_expectHybridSceneTraversalRecoveryForTesting){
                m_expectHybridSceneTraversalRecoveryForTesting = false;
                NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: test hybrid software traversal recovered"));
            }
#endif
        }
        else{
            bool reportHybridTraversalFailure = true;
#if !defined(NWB_FINAL)
            if(m_forceHybridSceneTraversalFallbackEveryFrameForTesting){
                reportHybridTraversalFailure = !m_reportedHybridSceneTraversalFallbackLoopFailureForTesting;
                m_reportedHybridSceneTraversalFallbackLoopFailureForTesting = true;
            }
#endif
            if(reportHybridTraversalFailure)
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hybrid transparent software shadow recording failed; transparent shadows absent this frame"));
            if(hybridSoftwareMaterialContextGraphOwned){
                // The immutable SW triple has already recorded in this packet. Replace it with the declared HW
                // fallback triple before accepting opaque shadows; a mismatched fallback rejects the merged packet.
                discardHybridGraphMaterialContext();
                if(!recordPreparedHybridHardwareMaterialContextFallback(
                    commandList,
                    hybridHardwareFallbackInstanceMaterialData,
                    hybridHardwareFallbackInstanceMaterialByteCount,
                    hybridHardwareFallbackInstanceData,
                    hybridHardwareFallbackInstanceByteCount,
                    hybridHardwareFallbackMaterialTypedData,
                    hybridHardwareFallbackMaterialTypedByteCount
                )){
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen hybrid hardware material-context restore failed; rejecting shadow preparation packet"));
                    return false;
                }
            }
        }
    }
    // A graph-owned hybrid plan may exist only for the optional software tail. It is eligible for the common
    // acceptance callback exactly when its commands were emitted; all other HW-only early-outs retain their native
    // fallback without falsely advancing the SW-BVH CPU cache.
    if(meshSwBvhBuildsGraphOwned && !hybridMeshSwBvhBuildRecorded)
        clearPreparedMeshSwBvhBuilds();
    // The graph may already contain the immutable pair upload, but only a restored traversal table or successful
    // immutable traversal may publish its static-scene cache. An optional-tail miss clears the retained pair before
    // the common acceptance callback can observe it.
    if(sceneBvhBatchGraphOwned && !hybridSceneBvhBuildRecorded)
        clearPreparedSceneBvh();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

