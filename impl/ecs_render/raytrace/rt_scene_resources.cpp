// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_system.h"
#include <impl/ecs_render/raytrace/ray_trace_material_resources.h>
#include "task_graph_scene_resources.h"
#include "rt_private.h"
#include "renderer_raytracing_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::prepareSceneQueryResources(){
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(
        !m_shadowVisibilityHardwareSupported
        || !m_shadowVisibilityTraceResourcesPreflighted
        || !m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
        || !heap.isInitialized()
        || !heap.hasAccelStructLayout()
        || !m_rayTracingState.m_tlas
        || !m_rayTracingState.m_tlasHeapHandle.valid()
        || !m_rayTracingState.m_shadowInstanceMaterialBuffer
        || !m_rayTracingState.m_shadowMaterialTypedBuffer
        || !m_rayTracingState.m_shadowInstanceBuffer
        || !m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer
    )
        return false;
    return RayTraceMaterialResources(m_graphics, m_rayTracingState, m_rayTracingState).ensureRayTraceMaterialContextSlotsHeapHandle();
}

RayTracingSceneGraphResources RendererRayTracingSystem::snapshotSceneGraphResources()const noexcept{
    const auto& state = m_rayTracingState;
    RayTracingSceneGraphResources resources;
    if(m_shadowVisibilityResourcesPreflighted && m_shadowVisibilityTraceResourcesPreflighted){
        resources.contentStamp = m_preparedSceneContentStamp;
        resources.opticalScene = m_shadowVisibilityHardwareSupported ? m_hardwareOpticalScene.snapshot() : m_softwareOpticalScene.snapshot();
    }
    if(!m_shadowVisibilityHardwareSupported || !m_shadowVisibilityTraceResourcesPreflighted || !state.m_rayTraceMaterialContextSlotsHeapHandle.valid())
        return resources;
    resources.sceneTlas = state.m_tlas;
    resources.tlasHeapHandle = state.m_tlasHeapHandle;
    resources.materialContextSlotsBuffer = state.m_rayTraceMaterialContextSlotsBuffer;
    resources.instanceMaterialBuffer = state.m_shadowInstanceMaterialBuffer;
    resources.materialTypedBuffer = state.m_shadowMaterialTypedBuffer;
    resources.instanceBuffer = state.m_shadowInstanceBuffer;
    if(m_lightSpaceShadow.m_csg.snapshot.hasCsg){
        resources.csgTraceContextBuffer = state.m_csgTraceContextBuffer;
        resources.csgTraceBoundsBuffers = m_lightSpaceShadow.m_csg.dynamicBounds;
    }
    resources.materialContextSlotsHeapSlot = state.m_rayTraceMaterialContextSlotsHeapHandle.slot();
    resources.hardwareAvailable = true;
    return resources;
}

Expected<RayTraceMaterialContextSlots> RendererRayTracingSystem::snapshotRayTraceMaterialContextSlots(){
    // The shared graph has already imported this constant buffer and its UniformBuffer descriptor by the time its preparation packet records.
    // Never recreate here: a recording-time replacement would break frozen identity and the selector snapshot.
    if(!m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer || !m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.valid() || m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer)
        return MakeUnexpected(Failure{});

    RayTraceMaterialContextSlots slots;
    const auto resolveStorageSlot = [](const Core::Buffer* buffer, const Core::GpuDescriptorHandle handle)noexcept -> Expected<u32>{
        if(!buffer)
            return 0u;
        if(!handle.valid() || handle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer)
            return MakeUnexpected(Failure{});
        return handle.slot();
    };

    const auto sceneBvhNodes = resolveStorageSlot(m_rayTracingState.m_sceneBvhNodeBuffer.get(), m_rayTracingState.m_sceneBvhNodeHeapHandle);
    if(!sceneBvhNodes){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: ray-trace material-context heap registration is incomplete"));
        return MakeUnexpected(Failure{});
    }
    slots.sceneBvhNodes = *sceneBvhNodes;
    const auto sceneInstances = resolveStorageSlot(m_rayTracingState.m_sceneInstanceBuffer.get(), m_rayTracingState.m_sceneInstanceHeapHandle);
    if(!sceneInstances){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: ray-trace material-context heap registration is incomplete"));
        return MakeUnexpected(Failure{});
    }
    slots.sceneInstances = *sceneInstances;
    const auto instanceMaterial = resolveStorageSlot(m_rayTracingState.m_shadowInstanceMaterialBuffer.get(), m_rayTracingState.m_shadowInstanceMaterialHeapHandle);
    if(!instanceMaterial){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: ray-trace material-context heap registration is incomplete"));
        return MakeUnexpected(Failure{});
    }
    slots.instanceMaterial = *instanceMaterial;
    const auto materialTyped = resolveStorageSlot(m_rayTracingState.m_shadowMaterialTypedBuffer.get(), m_rayTracingState.m_shadowMaterialTypedHeapHandle);
    if(!materialTyped){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: ray-trace material-context heap registration is incomplete"));
        return MakeUnexpected(Failure{});
    }
    slots.materialTyped = *materialTyped;
    const auto meshInstances = resolveStorageSlot(m_rayTracingState.m_shadowInstanceBuffer.get(), m_rayTracingState.m_shadowInstanceHeapHandle);
    if(!meshInstances){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: ray-trace material-context heap registration is incomplete"));
        return MakeUnexpected(Failure{});
    }
    slots.meshInstances = *meshInstances;

    // Optical metadata follows the selected backend's instance order and retains its preflighted descriptor.
    const RayTracingOpticalSceneSnapshot opticalScene = m_shadowVisibilityHardwareSupported ? m_hardwareOpticalScene.snapshot() : m_softwareOpticalScene.snapshot();
    if(opticalScene.valid()){
        slots.opticalScene = opticalScene.descriptor.slot();
        slots.opticalInstanceCount = opticalScene.upload->instanceCount;
    }

    if(m_shadowVisibilityTraceResourcesPreflighted && m_lightSpaceShadow.m_csg.snapshot.hasCsg){
        const auto csgContext = resolveStorageSlot(m_rayTracingState.m_csgTraceContextBuffer.get(), m_rayTracingState.m_csgTraceContextHeapHandle);
        if(!csgContext || !m_rayTracingState.m_csgTraceContextBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: current CSG ray context is unavailable"));
            return MakeUnexpected(Failure{});
        }
        slots.csgContext = *csgContext;
    }

    return slots;
}

void RayTraceMaterialResources::releaseRayTraceMaterialContextHeapHandles(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        heap.free(m_scene.m_sceneBvhNodeHeapHandle);
        heap.free(m_scene.m_sceneInstanceHeapHandle);
        heap.free(m_shadow.m_shadowInstanceMaterialHeapHandle);
        heap.free(m_shadow.m_shadowMaterialTypedHeapHandle);
        heap.free(m_shadow.m_shadowInstanceHeapHandle);
        heap.free(m_scene.m_rayTraceMaterialContextSlotsHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_scene.m_csgTraceContextHeapHandle);
        heap.free(m_shadow.m_swShadowEdgeCounterHeapHandle);
        heap.free(m_shadow.m_swShadowEdgeListHeapHandle);
        heap.free(m_shadow.m_swShadowIndirectArgsHeapHandle);
    }
    m_scene.m_sceneBvhNodeHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_scene.m_sceneInstanceHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_shadow.m_shadowInstanceMaterialHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_shadow.m_shadowMaterialTypedHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_shadow.m_shadowInstanceHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_scene.m_rayTraceMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_scene.m_csgTraceContextHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_shadow.m_swShadowEdgeCounterHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_shadow.m_swShadowEdgeListHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_shadow.m_swShadowIndirectArgsHeapHandle = Core::GpuDescriptorHandle::Invalid();
}

b8 RayTraceMaterialResources::ensureRayTraceMaterialContextSlotsBuffer(){
    if(m_scene.m_rayTraceMaterialContextSlotsBuffer)
        return true;

    Core::BufferDesc slotsBufferDesc;
    slotsBufferDesc
        .setByteSize(sizeof(RayTraceMaterialContextSlots))
        .setIsConstantBuffer(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("raytrace_material_context_slots"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto rayTraceMaterialContextSlotsBufferResult = m_graphics.createBuffer(slotsBufferDesc);
    if(rayTraceMaterialContextSlotsBufferResult)
        m_scene.m_rayTraceMaterialContextSlotsBuffer = Move(*rayTraceMaterialContextSlotsBufferResult);
    else
        m_scene.m_rayTraceMaterialContextSlotsBuffer.reset();
    if(!rayTraceMaterialContextSlotsBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create ray-trace material-context slot buffer"));
        return false;
    }
    return true;
}

b8 RayTraceMaterialResources::ensureRayTraceMaterialContextSlotsHeapHandle(){
    if(!ensureRayTraceMaterialContextSlotsBuffer())
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::UniformBuffer, false, heap, *m_scene.m_rayTraceMaterialContextSlotsBuffer.get(), m_scene.m_rayTraceMaterialContextSlotsHeapHandle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register ray-trace material-context selector in the descriptor heap"));
        return false;
    }
    return true;
}

b8 RayTraceMaterialResources::ensureRayTraceMaterialContextHeapHandle(Core::Buffer& buffer, Core::GpuDescriptorHandle& handle){
    if(handle.valid())
        return true;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: ray-trace material context requires the initialized global descriptor heap"));
        return false;
    }

    if(!RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, false, heap, buffer, handle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register ray-trace material context buffer in the descriptor heap"));
        return false;
    }
    return true;
}

b8 RayTraceMaterialResources::replaceRayTraceMaterialContextHeapHandle(Core::Buffer& buffer, Core::GpuDescriptorHandle& handle){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: ray-trace material context requires the initialized global descriptor heap"));
        return false;
    }

    if(!RayTracingDetail::ReplaceHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, false, heap, buffer, handle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to replace ray-trace material-context heap descriptor"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

