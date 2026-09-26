// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <core/task/gpu/compiled_graph.h>
#include <global/algorithm.h>
#include <impl/ecs_render/raytrace/rt_shadow_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureRayTraceMaterialContextHeapHandle(Core::Buffer& buffer, Core::GpuDescriptorHandle& handle){
    if(handle.valid())
        return true;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: ray-trace material context requires the initialized global descriptor heap"));
        return false;
    }

    if(!RayTracingDetail::EnsureHeapBuffer(heap, buffer, Core::GpuDescriptorClass::StorageBuffer, false, handle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register ray-trace material context buffer in the descriptor heap"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::replaceRayTraceMaterialContextHeapHandle(Core::Buffer& buffer, Core::GpuDescriptorHandle& handle){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: ray-trace material context requires the initialized global descriptor heap"));
        return false;
    }

    if(!RayTracingDetail::ReplaceHeapBuffer(heap, buffer, Core::GpuDescriptorClass::StorageBuffer, false, handle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to replace ray-trace material-context heap descriptor"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureRayTraceMaterialContextSlotsBuffer(){
    if(m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer)
        return true;

    Core::BufferDesc slotsBufferDesc;
    slotsBufferDesc
        .setByteSize(sizeof(RayTraceMaterialContextSlots))
        .setIsConstantBuffer(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("raytrace_material_context_slots"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer = m_graphics.createBuffer(slotsBufferDesc);
    if(!m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create ray-trace material-context slot buffer"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::snapshotRayTraceMaterialContextSlots(RayTraceMaterialContextSlots& outSlots){
    // The shared graph has already imported this constant buffer and its UniformBuffer descriptor by the time its
    // preparation packet records. Do not recreate either one here: a recording-time replacement would invalidate
    // the graph's frozen resource identity and the immutable selector snapshot it retains.
    if(
        !m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer
        || !m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.valid()
        || m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.descriptorClass() != Core::GpuDescriptorClass::UniformBuffer
    )
        return false;

    RayTraceMaterialContextSlots slots;
    const auto resolveStorageSlot = [](const Core::Buffer* buffer, const Core::GpuDescriptorHandle handle, u32& outSlot) -> bool{
        if(!buffer){
            outSlot = 0u;
            return true;
        }
        if(!handle.valid() || handle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer)
            return false;
        outSlot = handle.slot();
        return true;
    };

    const bool complete =
        resolveStorageSlot(m_rayTracingState.m_sceneBvhNodeBuffer.get(), m_rayTracingState.m_sceneBvhNodeHeapHandle, slots.sceneBvhNodes)
        && resolveStorageSlot(m_rayTracingState.m_sceneInstanceBuffer.get(), m_rayTracingState.m_sceneInstanceHeapHandle, slots.sceneInstances)
        && resolveStorageSlot(m_rayTracingState.m_shadowInstanceMaterialBuffer.get(), m_rayTracingState.m_shadowInstanceMaterialHeapHandle, slots.instanceMaterial)
        && resolveStorageSlot(m_rayTracingState.m_shadowMaterialTypedBuffer.get(), m_rayTracingState.m_shadowMaterialTypedHeapHandle, slots.materialTyped)
        && resolveStorageSlot(m_rayTracingState.m_shadowInstanceBuffer.get(), m_rayTracingState.m_shadowInstanceHeapHandle, slots.meshInstances)
    ;
    if(!complete){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: ray-trace material-context heap registration is incomplete"));
        return false;
    }

    // Optical metadata follows the selected backend's instance order and retains its preflighted descriptor.
    const RayTracingOpticalSceneSnapshot opticalScene = m_shadowVisibilityHardwareSupported
        ? m_hardwareOpticalScene.snapshot() : m_softwareOpticalScene.snapshot();
    if(opticalScene.valid()){
        slots.opticalScene = opticalScene.descriptor.slot();
        slots.opticalInstanceCount = opticalScene.upload->instanceCount;
    }

    outSlots = slots;
    return true;
}

void RendererRayTracingSystem::releaseRayTraceMaterialContextHeapHandles(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        heap.free(m_rayTracingState.m_sceneBvhNodeHeapHandle);
        heap.free(m_rayTracingState.m_sceneInstanceHeapHandle);
        heap.free(m_rayTracingState.m_shadowInstanceMaterialHeapHandle);
        heap.free(m_rayTracingState.m_shadowMaterialTypedHeapHandle);
        heap.free(m_rayTracingState.m_shadowInstanceHeapHandle);
        heap.free(m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle);
        heap.free(m_rayTracingState.m_swShadowEdgeCounterHeapHandle);
        heap.free(m_rayTracingState.m_swShadowEdgeListHeapHandle);
        heap.free(m_rayTracingState.m_swShadowIndirectArgsHeapHandle);
    }
    m_rayTracingState.m_sceneBvhNodeHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_sceneInstanceHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_shadowInstanceMaterialHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_shadowMaterialTypedHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_shadowInstanceHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_swShadowEdgeCounterHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_swShadowEdgeListHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTracingState.m_swShadowIndirectArgsHeapHandle = Core::GpuDescriptorHandle::invalid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

