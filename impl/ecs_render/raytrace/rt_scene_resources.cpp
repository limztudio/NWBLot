// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scene_resources.h"
#include "rt_private.h"
#include "renderer_raytracing_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureRayTraceMaterialContextSlotsHeapHandle(){
    if(!ensureRayTraceMaterialContextSlotsBuffer())
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !RayTracingDetail::EnsureHeapBuffer(
            heap,
            *m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer.get(),
            Core::GpuDescriptorClass::UniformBuffer,
            false,
            m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register ray-trace material-context selector in the descriptor heap"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::prepareSceneQueryResources(){
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
    return ensureRayTraceMaterialContextSlotsHeapHandle();
}

RayTracingSceneGraphResources RendererRayTracingSystem::snapshotSceneGraphResources()const{
    const auto& state = m_rayTracingState;
    RayTracingSceneGraphResources resources;
    if(m_shadowVisibilityResourcesPreflighted && m_shadowVisibilityTraceResourcesPreflighted)
        resources.contentStamp = m_preparedSceneContentStamp;
    if(
        !m_shadowVisibilityHardwareSupported
        || !m_shadowVisibilityTraceResourcesPreflighted
        || !state.m_rayTraceMaterialContextSlotsHeapHandle.valid()
    )
        return resources;
    resources.sceneTlas = state.m_tlas;
    resources.tlasHeapHandle = state.m_tlasHeapHandle;
    resources.materialContextSlotsBuffer = state.m_rayTraceMaterialContextSlotsBuffer;
    resources.instanceMaterialBuffer = state.m_shadowInstanceMaterialBuffer;
    resources.materialTypedBuffer = state.m_shadowMaterialTypedBuffer;
    resources.instanceBuffer = state.m_shadowInstanceBuffer;
    resources.materialContextSlotsHeapSlot = state.m_rayTraceMaterialContextSlotsHeapHandle.slot();
    resources.hardwareAvailable = true;
    return resources;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

